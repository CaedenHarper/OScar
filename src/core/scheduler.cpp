#include "scheduler.hpp"

#include "context.hpp"
#include "interrupts.hpp"
#include "panic.hpp"
#include "scheduler_internal.hpp"
#include "thread.hpp"
#include "thread_internal.hpp"

namespace scheduler {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) scheduler state must survive context switches
kernel_thread::Thread* g_ready_head = nullptr;
kernel_thread::Thread* g_ready_tail = nullptr;
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

kernel_thread::Thread* next_locked() {
    auto* thread = dequeue_locked();
    return thread == nullptr ? g_idle_thread : thread;
}

void reset_time_slice(kernel_thread::Thread* thread) {
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
    if(current_thread != g_idle_thread && g_ready_head != nullptr) {
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

kernel_thread::Thread* current() {
    return g_current_thread;
}

kernel_thread::Thread* idle() {
    return g_idle_thread;
}

void timer_tick() {
    if(!g_initialized || !g_started || g_current_thread == nullptr || g_current_thread == g_idle_thread) {
        return;
    }

    if(g_current_thread->time_slice_remaining > 0) {
        --g_current_thread->time_slice_remaining;
    }
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
    context::switch_context(&g_bootstrap_context, &next_thread->cpu_context);
    park();
}

[[noreturn]] void thread_exit(kernel_thread::Thread* thread) {
    if(!g_started || thread == nullptr || thread != g_current_thread) {
        park();
    }

    interrupts::disable();
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

    g_reschedule_pending = false;
    schedule(false);
}

} // namespace scheduler
