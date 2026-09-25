#pragma once

#include "process.hpp"
#include "thread.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace process {

struct Process {
    ProcessId id;
    State state;
    virtual_memory::AddressSpace address_space;
    kernel_thread::Thread* thread_head;
    kernel_thread::Thread* thread_tail;
    uint32_t thread_count;
};

bool attach_thread(Process* process, kernel_thread::Thread* thread);
bool detach_thread(kernel_thread::Thread* thread);

} // namespace process
