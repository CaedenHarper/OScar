#include "scheduler.hpp"

#include "context.hpp"
#include "interrupts.hpp"
#include "panic.hpp"
#include "process_internal.hpp"
#include "scheduler_internal.hpp"
#include "thread.hpp"
#include "thread_internal.hpp"
#include "timer.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace scheduler {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) scheduler state must survive context switches
kernel_thread::Thread* g_ready_head = nullptr;
kernel_thread::Thread* g_ready_tail = nullptr;
kernel_thread::Thread* g_waiting_head = nullptr;
kernel_thread::Thread* g_current_thread = nullptr;
kernel_thread::Thread* g_idle_thread = nullptr;
context::CpuContext g_bootstrap_context = {};
bool g_initialized = false;
bool g_started = false;
bool g_reschedule_pending = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[noreturn]] void park() {
    for(;;) {
        asm volatile("cli; hlt" : : : "memory");
    }
}

void idle_entry(void* argument) {
    (void)argument;
    for(;;) {
        asm volatile("hlt" : : : "memory");
    }
}

bool enqueue_locked(kernel_thread::Thread* thread) {
    if(thread == nullptr || thread->idle || thread->queued || thread->state != kernel_thread::State::Ready) {
        return false;
    }

    thread->ready_next = nullptr;
    thread->queued = true;
    if(g_ready_tail == nullptr) {
        g_ready_head = thread;
        g_ready_tail = thread;
    } else {
        g_ready_tail->ready_next = thread;
        g_ready_tail = thread;
    }
    return true;
}

kernel_thread::Thread* dequeue_locked() {
    auto* thread = g_ready_head;
    if(thread == nullptr) {
        return nullptr;
    }

    g_ready_head = thread->ready_next;
    if(g_ready_head == nullptr) {
        g_ready_tail = nullptr;
    }
    thread->ready_next = nullptr;
    thread->queued = false;
    return thread;
}

void enqueue_waiting_locked(kernel_thread::Thread* thread, uint64_t wake_tick) {
    thread->wake_tick = wake_tick;
    thread->waiting = true;
    thread->wait_queue = nullptr;
    thread->wait_reason = kernel_thread::WaitReason::Sleeping;
    thread->waiting_next = nullptr;

    // Keep the earliest deadline at the head so each timer tick examines only the
    // expired prefix instead of scanning every waiting thread.
    kernel_thread::Thread** link = &g_waiting_head;
    while(*link != nullptr && (*link)->wake_tick <= wake_tick) {
        link = &(*link)->waiting_next;
    }
    thread->waiting_next = *link;
    *link = thread;
}

void wake_expired_locked(uint64_t current_tick) {
    while(g_waiting_head != nullptr && g_waiting_head->wake_tick <= current_tick) {
        auto* thread = g_waiting_head;
        g_waiting_head = thread->waiting_next;
        thread->waiting_next = nullptr;
        thread->waiting = false;
        thread->wait_reason = kernel_thread::WaitReason::None;
        thread->state = kernel_thread::State::Ready;
        // A waiting thread is converted back to Ready before entering the same queue used
        // by ordinary yields, preserving one FIFO scheduling policy after wake-up.
        (void)enqueue_locked(thread);
    }
}

kernel_thread::Thread* next_locked() {
    // The idle thread is a real context rather than a null sentinel, so the scheduler
    // always has a valid stack to switch to when all ordinary threads are waiting.
    auto* thread = dequeue_locked();
    return thread == nullptr ? g_idle_thread : thread;
}

void reset_time_slice(kernel_thread::Thread* thread) {
    // Idle work has no fairness budget; assigning it a slice would make its accounting
    // indistinguishable from a runnable thread and could create needless reschedules.
    if(thread != nullptr && thread != g_idle_thread) {
        thread->time_slice_remaining = kDefaultTimeSliceTicks;
    }
}

void schedule(bool restore_interrupts) {
    if(!g_initialized || !g_started || g_current_thread == nullptr) {
        return;
    }

    interrupts::disable();
    auto* current_thread = g_current_thread;
    auto* next_thread = current_thread;
    if(current_thread == g_idle_thread && g_ready_head != nullptr) {
        // The idle thread is not put on the ready queue. When a timer wakes a thread,
        // select that queued thread explicitly instead of treating idle as runnable work.
        next_thread = next_locked();
    } else if(current_thread != g_idle_thread && g_ready_head != nullptr) {
        current_thread->state = kernel_thread::State::Ready;
        if(!enqueue_locked(current_thread)) {
            current_thread->state = kernel_thread::State::Running;
            if(restore_interrupts) {
                interrupts::enable();
            }
            return;
        }
        next_thread = next_locked();
    }

    // A sole runnable thread is already executing. Avoid switching to its own saved
    // context, which would add no scheduling progress and could overwrite its queue state.
    if(next_thread != current_thread) {
        next_thread->state = kernel_thread::State::Running;
        reset_time_slice(next_thread);
        g_current_thread = next_thread;
        context::switch_context(&current_thread->cpu_context, &next_thread->cpu_context);
    }
    if(restore_interrupts) {
        interrupts::enable();
    }
}

} // namespace

bool initialize() {
    if(g_initialized) {
        return false;
    }

    auto* idle_thread = kernel_thread::create(idle_entry, nullptr);
    if(idle_thread == nullptr) {
        return false;
    }

    idle_thread->idle = true;
    idle_thread->scheduler_managed = true;
    g_idle_thread = idle_thread;
    g_initialized = true;
    return true;
}

bool enqueue(kernel_thread::Thread* thread) {
    if(!g_initialized || thread == nullptr || thread->scheduler_managed) {
        return false;
    }

    // Mark ownership only after queue insertion succeeds; callers can safely retry a
    // failed enqueue without the scheduler believing it already owns the thread.
    if(!enqueue_locked(thread)) {
        return false;
    }
    thread->scheduler_managed = true;
    reset_time_slice(thread);
    return true;
}

bool remove(kernel_thread::Thread* thread) {
    if(!g_initialized || thread == nullptr || !thread->queued) {
        return false;
    }

    kernel_thread::Thread* previous = nullptr;
    for(auto* candidate = g_ready_head; candidate != nullptr; candidate = candidate->ready_next) {
        if(candidate != thread) {
            previous = candidate;
            continue;
        }

        if(previous == nullptr) {
            g_ready_head = candidate->ready_next;
        } else {
            previous->ready_next = candidate->ready_next;
        }
        if(g_ready_tail == candidate) {
            g_ready_tail = previous;
        }
        candidate->ready_next = nullptr;
        candidate->queued = false;
        return true;
    }
    return false;
}

bool block_current(synchronization::WaitQueue* queue) {
    if(!g_initialized || !g_started || g_current_thread == nullptr || g_current_thread == g_idle_thread ||
       queue == nullptr) {
        return false;
    }

    auto* current_thread = g_current_thread;
    if(!synchronization::enqueue_locked(queue, current_thread)) {
        return false;
    }

    current_thread->wait_reason = kernel_thread::WaitReason::Waiting;
    current_thread->state = kernel_thread::State::Waiting;
    auto* next_thread = next_locked();
    next_thread->state = kernel_thread::State::Running;
    reset_time_slice(next_thread);
    g_current_thread = next_thread;
    context::switch_context(&current_thread->cpu_context, &next_thread->cpu_context);
    return true;
}

bool wake_one(synchronization::WaitQueue* queue) {
    if(!g_initialized || queue == nullptr) {
        return false;
    }

    auto* thread = synchronization::dequeue_locked(queue);
    if(thread == nullptr) {
        return false;
    }

    thread->wait_reason = kernel_thread::WaitReason::None;
    thread->state = kernel_thread::State::Ready;
    if(!enqueue_locked(thread)) {
        return false;
    }
    if(g_current_thread == g_idle_thread) {
        g_reschedule_pending = true;
    }
    return true;
}

uint32_t wake_all(synchronization::WaitQueue* queue) {
    uint32_t count = 0;
    while(wake_one(queue)) {
        ++count;
    }
    return count;
}

kernel_thread::Thread* current() {
    return g_current_thread;
}

kernel_thread::Thread* idle() {
    return g_idle_thread;
}

void timer_tick(uint64_t current_tick) {
    if(!g_initialized || !g_started || g_current_thread == nullptr) {
        return;
    }

    wake_expired_locked(current_tick);
    if(g_current_thread == g_idle_thread) {
        if(g_ready_head != nullptr) {
            g_reschedule_pending = true;
        }
        return;
    }

    if(g_current_thread->time_slice_remaining > 0) {
        --g_current_thread->time_slice_remaining;
    }
    // The timer interrupt only records work here. Switching in the middle of the timer
    // handler would bypass the common interrupt-exit path and make the saved frame unsafe.
    if(g_current_thread->time_slice_remaining == 0) {
        g_current_thread->time_slice_remaining = kDefaultTimeSliceTicks;
        g_reschedule_pending = true;
    }
}

void yield() {
    if(!g_initialized || !g_started || g_current_thread == nullptr || g_current_thread == g_idle_thread) {
        return;
    }
    schedule(true);
}

void sleep_until(uint64_t wake_tick) {
    if(!g_initialized || !g_started || g_current_thread == nullptr || g_current_thread == g_idle_thread) {
        return;
    }

    interrupts::disable();
    const uint64_t current_tick = timer::ticks();
    if(wake_tick <= current_tick) {
        // An expired deadline must not enter the wait queue; returning immediately keeps
        // callers from sleeping forever when a timeout was computed before this call.
        interrupts::enable();
        return;
    }

    auto* current_thread = g_current_thread;
    enqueue_waiting_locked(current_thread, wake_tick);
    current_thread->state = kernel_thread::State::Waiting;
    auto* next_thread = next_locked();
    next_thread->state = kernel_thread::State::Running;
    reset_time_slice(next_thread);
    g_current_thread = next_thread;
    context::switch_context(&current_thread->cpu_context, &next_thread->cpu_context);
    interrupts::enable();
}

void sleep(uint64_t ticks) {
    if(ticks == 0) {
        // Zero-duration sleep is a cooperative yield rather than a queue insertion with
        // a deadline that is already due.
        yield();
        return;
    }

    const uint64_t current_tick = timer::ticks();
    const uint64_t wake_tick = ticks > UINT64_MAX - current_tick ? UINT64_MAX : current_tick + ticks;
    sleep_until(wake_tick);
}

[[noreturn]] void start() {
    if(!g_initialized || g_started) {
        panic::halt("scheduler cannot start in its current state");
    }

    interrupts::disable();
    g_started = true;
    auto* next_thread = next_locked();
    next_thread->state = kernel_thread::State::Running;
    reset_time_slice(next_thread);
    g_current_thread = next_thread;
    // Save the boot context only as a one-way origin. Once a thread runs, no scheduler
    // path returns to kmain, so the parked bootstrap context is never scheduled again.
    context::switch_context(&g_bootstrap_context, &next_thread->cpu_context);
    park();
}

[[noreturn]] void thread_exit(kernel_thread::Thread* thread) {
    if(!g_started || thread == nullptr || thread != g_current_thread) {
        park();
    }

    interrupts::disable();
    if(!process::detach_thread(thread)) {
        park();
    }
    // A terminated thread cannot remain on its own stack: its stack may be reclaimed
    // later, so transfer directly to another scheduler-owned context before parking.
    thread->state = kernel_thread::State::Terminated;
    auto* next_thread = next_locked();
    next_thread->state = kernel_thread::State::Running;
    reset_time_slice(next_thread);
    g_current_thread = next_thread;
    context::switch_context(&thread->cpu_context, &next_thread->cpu_context);
    park();
}

extern "C" void scheduler_interrupt_exit() {
    if(!g_reschedule_pending) {
        return;
    }

    // Clear the request before switching because the outgoing thread's next timer tick
    // must be allowed to create a fresh request rather than replaying this one forever.
    g_reschedule_pending = false;
    schedule(false);
}

} // namespace scheduler
