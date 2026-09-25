#pragma once

#include "context.hpp"
#include "thread.hpp"

#include <stdint.h>

namespace synchronization {
struct WaitQueue;
} // namespace synchronization

namespace kernel_thread {

enum class WaitReason : uint8_t {
    None,
    Sleeping,
    Waiting,
};

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
    process::Process* owner_process;
    context::CpuContext* return_context;
    Thread* ready_next;
    Thread* waiting_next;
    synchronization::WaitQueue* wait_queue;
    bool queued;
    bool waiting;
    bool scheduler_managed;
    bool idle;
    uint32_t time_slice_remaining;
    uint64_t wake_tick;
    WaitReason wait_reason;
    Thread* process_next;
};

} // namespace kernel_thread
