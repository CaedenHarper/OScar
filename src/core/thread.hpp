#pragma once

#include "context.hpp"

#include <stdint.h>

namespace virtual_memory {
struct AddressSpace;
} // namespace virtual_memory

namespace process {
struct Process;
} // namespace process

namespace kernel_thread {

using ThreadId = uint64_t;

enum class State : uint8_t {
    Ready,
    Waiting,
    Running,
    Terminated,
};

constexpr uint64_t kDefaultStackSize = 16ULL * 1024ULL;

struct Thread;

/**
 * Create a kernel thread with a private default-sized kernel stack. The
 * kernel heap must be initialized first. The returned thread starts in
 * State::Ready and has no address-space reference.
 */
Thread* create(context::Entry entry, void* argument);

/** Create a kernel thread with an explicit stack size and no address space. */
Thread* create(context::Entry entry, void* argument, uint64_t stack_size);

/**
 * Create a kernel thread with a private kernel stack. The kernel heap must be
 * initialized first. The returned thread starts in State::Ready and retains,
 * but does not own, the optional address-space reference.
 */
Thread* create(context::Entry entry, void* argument, uint64_t stack_size, virtual_memory::AddressSpace* address_space);

/** Create a thread associated with a process and its owned address space. */
Thread* create(process::Process* process, context::Entry entry, void* argument, uint64_t stack_size);

/** Create a process-associated thread using the default kernel stack size. */
Thread* create(process::Process* process, context::Entry entry, void* argument);

/**
 * Release a thread and its kernel stack. The thread must not be Running, and
 * the pointer must still refer to a live thread returned by create().
 */
bool destroy(Thread* thread);

/**
 * Switch to a Ready thread and return to return_context after its entry
 * function returns. Interrupts must be disabled by the caller. The thread
 * becomes Running while executing and Terminated after its entry returns.
 */
bool run(Thread* thread, context::CpuContext* return_context);

/** Return the thread's stable identifier, or zero for a null thread. */
ThreadId id(const Thread* thread);

/** Return the thread's current lifecycle state, or State::Terminated for null. */
State state(const Thread* thread);

/** Return the saved CPU context used by the scheduler to switch threads. */
context::CpuContext* context(Thread* thread);

/** Return the inclusive lower bound of the thread's allocated kernel stack. */
uintptr_t stack_bottom(const Thread* thread);

/** Return the exclusive upper bound of the thread's allocated kernel stack. */
uintptr_t stack_top(const Thread* thread);

/** Return the non-owning address-space reference associated with the thread. */
virtual_memory::AddressSpace* address_space(const Thread* thread);

/** Return the non-owning process reference associated with the thread. */
process::Process* owner_process(const Thread* thread);

/** Return the timer tick at which the thread is scheduled to leave State::Waiting. */
uint64_t wake_tick(const Thread* thread);

} // namespace kernel_thread
