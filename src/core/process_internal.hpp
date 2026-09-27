#pragma once

#include "process.hpp"
#include "thread.hpp"
#include "vfs.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace process {

constexpr uint32_t kMaximumFileDescriptors = 16;
constexpr uint32_t kFirstFileDescriptor = 3;

struct FileDescriptor {
    vfs::File file;
    bool open;
};

struct Process {
    ProcessId id;
    State state;
    virtual_memory::AddressSpace address_space;
    kernel_thread::Thread* thread_head;
    kernel_thread::Thread* thread_tail;
    uint32_t thread_count;
    FileDescriptor descriptors[kMaximumFileDescriptors];
};

bool attach_thread(Process* process, kernel_thread::Thread* thread);
bool detach_thread(kernel_thread::Thread* thread);

int32_t allocate_file_descriptor(Process* process, const vfs::File* file);
vfs::File* file_descriptor(Process* process, uint64_t descriptor);
bool close_file_descriptor(Process* process, uint64_t descriptor);
void close_file_descriptors(Process* process);

} // namespace process
