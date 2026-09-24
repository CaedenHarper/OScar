#include "thread.hpp"

#include "context.hpp"
#include "kernel_heap.hpp"

#include <stdint.h>

namespace kernel_thread {

struct Thread {
    ThreadId id;
    context::CpuContext cpu_context;
    void* stack;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    State state;
    context::Entry entry;
    void* argument;
    virtual_memory::AddressSpace* address_space;
    context::CpuContext* return_context;
};

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) thread IDs must remain unique across thread lifetimes
ThreadId g_next_thread_id = 1;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[noreturn]] void park_terminated_thread() {
    for(;;) {
        asm volatile("cli; hlt" : : : "memory");
    }
}

void thread_bootstrap(void* argument) {
    auto* thread = static_cast<Thread*>(argument);
    thread->state = State::Running;
    thread->entry(thread->argument);
    thread->state = State::Terminated;

    context::switch_context(&thread->cpu_context, thread->return_context);
    park_terminated_thread();
}

} // namespace

Thread* create(context::Entry entry, void* argument, uint64_t stack_size, virtual_memory::AddressSpace* address_space) {
    if(entry == nullptr || stack_size == 0 || g_next_thread_id == 0) {
        return nullptr;
    }

    auto* thread = static_cast<Thread*>(kernel_heap::allocate(sizeof(Thread)));
    if(thread == nullptr) {
        return nullptr;
    }

    auto* stack = static_cast<uint8_t*>(kernel_heap::allocate(stack_size));
    if(stack == nullptr) {
        kernel_heap::free(thread);
        return nullptr;
    }

    thread->id = g_next_thread_id++;
    thread->stack = stack;
    thread->stack_bottom = reinterpret_cast<uintptr_t>(stack);
    thread->stack_top = thread->stack_bottom + stack_size;
    thread->state = State::Ready;
    thread->entry = entry;
    thread->argument = argument;
    thread->address_space = address_space;
    thread->return_context = nullptr;

    if(!context::initialize(&thread->cpu_context, thread->stack_top, thread_bootstrap, thread)) {
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

bool destroy(Thread* thread) {
    if(thread == nullptr || thread->state == State::Running) {
        return false;
    }

    const bool stack_freed = kernel_heap::free(thread->stack);
    const bool thread_freed = kernel_heap::free(thread);
    return stack_freed && thread_freed;
}

bool run(Thread* thread, context::CpuContext* return_context) {
    if(thread == nullptr || return_context == nullptr || thread->state != State::Ready) {
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

} // namespace kernel_thread
