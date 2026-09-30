#pragma once

#include "process.hpp"
#include "thread.hpp"
#include "vfs.hpp"
#include "virtual_memory.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace process {

constexpr uint32_t kMaximumFileDescriptors = 16;
constexpr uint32_t kFirstFileDescriptor = 3;
constexpr uint32_t kStandardInput = 0;
constexpr uint32_t kStandardOutput = 1;
constexpr uint32_t kStandardError = 2;
constexpr uint32_t kMaximumWorkingDirectoryLength = 511;

enum class DescriptorKind : uint8_t {
    Invalid,
    StandardInput,
    StandardOutput,
    StandardError,
    File,
};

struct FileDescriptor {
    DescriptorKind kind;
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
    char working_directory[kMaximumWorkingDirectoryLength + 1];
    Process* parent;
    Process* child_head;
    Process* child_tail;
    Process* sibling_next;
    synchronization::WaitQueue child_waiters;
    int64_t exit_status;
    char image_path[kMaximumImagePathLength + 1];
    Process* all_next;
};

bool attach_thread(Process* process, kernel_thread::Thread* thread);
bool detach_thread(kernel_thread::Thread* thread);
bool set_parent(Process* child, Process* parent);
void record_exit(Process* process, int64_t status);
Process* find_child_locked(Process* parent, ProcessId child_id);
bool reap_child_locked(Process* parent, Process* child, int64_t* status);
bool has_parent(const Process* process);
void set_image_path(Process* process, const char* path);
bool inherit_descriptors(Process* child, const Process* parent);

int32_t allocate_file_descriptor(Process* process, const vfs::File* file);
vfs::File* file_descriptor(Process* process, uint64_t descriptor);
DescriptorKind descriptor_kind(const Process* process, uint64_t descriptor);
bool close_file_descriptor(Process* process, uint64_t descriptor);
void close_file_descriptors(Process* process);

} // namespace process
