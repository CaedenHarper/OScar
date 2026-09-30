#include "thread.hpp"

#include "context.hpp"
#include "kernel_heap.hpp"
#include "process_internal.hpp"
#include "scheduler_internal.hpp"
#include "thread_internal.hpp"

#include <stdint.h>

namespace kernel_thread {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) thread IDs must remain unique across thread lifetimes
ThreadId g_next_thread_id = 1;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[noreturn]] void park_terminated_thread() {
    for(;;) {
        asm volatile("cli; hlt" : : : "memory");
    }
}

void user_placeholder(void* /*unused*/) {
    park_terminated_thread();
}

void thread_bootstrap(void* argument) {
    auto* thread = static_cast<Thread*>(argument);
    thread->state = State::Running;
    thread->entry(thread->argument);

    // Scheduler-managed threads must exit through the scheduler so their stack is never
    // selected again. Standalone threads instead return to the context supplied by run().
    if(thread->scheduler_managed) {
        scheduler::thread_exit(thread, 0);
    }

    if(!process::detach_thread(thread)) {
        park_terminated_thread();
    }
    thread->state = State::Terminated;

    context::switch_context(&thread->cpu_context, thread->return_context);
    park_terminated_thread();
}

} // namespace

Thread* create(context::Entry entry, void* argument, uint64_t stack_size, virtual_memory::AddressSpace* address_space) {
    if(entry == nullptr || stack_size == 0 || g_next_thread_id == 0) {
        return nullptr;
    }

    // Allocate the descriptor and stack separately: the descriptor remains reachable
    // after a context switch, while the stack is the independently owned execution area.
    auto* thread = static_cast<Thread*>(kernel_heap::allocate(sizeof(Thread)));
    if(thread == nullptr) {
        return nullptr;
    }

    auto* stack = static_cast<uint8_t*>(kernel_heap::allocate(stack_size));
    if(stack == nullptr) {
        // Roll back the descriptor immediately so a partially created thread cannot leak
        // heap space or appear in later allocation diagnostics.
        kernel_heap::free(thread);
        return nullptr;
    }

    // Initialize every field explicitly because this freestanding kernel does not rely
    // on hosted-runtime zero initialization or constructors for heap objects.
    thread->id = g_next_thread_id++;
    thread->stack = stack;
    thread->stack_bottom = reinterpret_cast<uintptr_t>(stack);
    thread->stack_top = thread->stack_bottom + stack_size;
    thread->state = State::Ready;
    thread->entry = entry;
    thread->argument = argument;
    thread->address_space = address_space;
    thread->owner_process = nullptr;
    thread->return_context = nullptr;
    thread->ready_next = nullptr;
    thread->waiting_next = nullptr;
    thread->wait_queue = nullptr;
    thread->queued = false;
    thread->waiting = false;
    thread->scheduler_managed = false;
    thread->idle = false;
    thread->user_mode = false;
    thread->time_slice_remaining = 0;
    thread->wake_tick = 0;
    thread->wait_reason = WaitReason::None;
    thread->process_next = nullptr;
    thread->terminated_process = nullptr;
    thread->reap_next = nullptr;

    if(!context::initialize(&thread->cpu_context, thread->stack_top, thread_bootstrap, thread)) {
        // Context construction is the final fallible step; release both allocations while
        // the descriptor still contains the original stack pointer.
        kernel_heap::free(stack);
        kernel_heap::free(thread);
        return nullptr;
    }
    return thread;
}

Thread* create(context::Entry entry, void* argument) {
    return create(entry, argument, kDefaultStackSize, nullptr);
}

Thread* create(context::Entry entry, void* argument, uint64_t stack_size) {
    return create(entry, argument, stack_size, nullptr);
}

Thread* create(process::Process* process, context::Entry entry, void* argument, uint64_t stack_size) {
    if(process == nullptr) {
        return nullptr;
    }

    auto* thread = create(entry, argument, stack_size, &process->address_space);
    if(thread == nullptr || !process::attach_thread(process, thread)) {
        if(thread != nullptr) {
            (void)destroy(thread);
        }
        return nullptr;
    }
    return thread;
}

Thread* create(process::Process* process, context::Entry entry, void* argument) {
    return create(process, entry, argument, kDefaultStackSize);
}

Thread* create_user(process::Process* process, uintptr_t user_entry, uintptr_t user_stack) {
    return create_user(process, user_entry, user_stack, 0, 0);
}

Thread* create_user(
    process::Process* process,
    uintptr_t user_entry,
    uintptr_t user_stack,
    uint64_t argument_count,
    uintptr_t argument_vector
) {
    if(process == nullptr) {
        return nullptr;
    }

    auto* thread = create(process, user_placeholder, nullptr, kDefaultStackSize);
    if(thread == nullptr ||
       !context::initialize_user(
           &thread->cpu_context, thread->stack_top, user_entry, user_stack, argument_count, argument_vector
       )) {
        if(thread != nullptr) {
            (void)destroy(thread);
        }
        return nullptr;
    }
    thread->user_mode = true;
    return thread;
}

bool destroy(Thread* thread) {
    // Queued or scheduler-managed threads may still be referenced by scheduler state;
    // freeing them here would leave an intrusive queue link pointing into reclaimed heap.
    if(thread == nullptr || thread->state == State::Running || thread->queued ||
       (thread->scheduler_managed && thread->state != State::Terminated)) {
        return false;
    }

    if(!process::detach_thread(thread)) {
        return false;
    }

    const bool stack_freed = kernel_heap::free(thread->stack);
    const bool thread_freed = kernel_heap::free(thread);
    return stack_freed && thread_freed;
}

bool run(Thread* thread, context::CpuContext* return_context) {
    if(thread == nullptr || return_context == nullptr || thread->state != State::Ready || thread->scheduler_managed) {
        return false;
    }

    thread->return_context = return_context;
    context::switch_context(return_context, &thread->cpu_context);
    return thread->state == State::Terminated;
}

ThreadId id(const Thread* thread) {
    return thread == nullptr ? 0 : thread->id;
}

State state(const Thread* thread) {
    return thread == nullptr ? State::Terminated : thread->state;
}

context::CpuContext* context(Thread* thread) {
    return thread == nullptr ? nullptr : &thread->cpu_context;
}

uintptr_t stack_bottom(const Thread* thread) {
    return thread == nullptr ? 0 : thread->stack_bottom;
}

uintptr_t stack_top(const Thread* thread) {
    return thread == nullptr ? 0 : thread->stack_top;
}

virtual_memory::AddressSpace* address_space(const Thread* thread) {
    return thread == nullptr ? nullptr : thread->address_space;
}

process::Process* owner_process(const Thread* thread) {
    return thread == nullptr ? nullptr : thread->owner_process;
}

bool is_user(const Thread* thread) {
    return thread != nullptr && thread->user_mode;
}

uint64_t wake_tick(const Thread* thread) {
    return thread == nullptr ? 0 : thread->wake_tick;
}

} // namespace kernel_thread
