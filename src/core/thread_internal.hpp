#pragma once

#include "context.hpp"
#include "thread.hpp"

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
    Thread* ready_next;
    bool queued;
    bool scheduler_managed;
    bool idle;
    uint32_t time_slice_remaining;
};

} // namespace kernel_thread
