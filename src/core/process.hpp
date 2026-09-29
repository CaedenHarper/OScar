#pragma once

#include <stdint.h>

namespace virtual_memory {
struct AddressSpace;
} // namespace virtual_memory

namespace synchronization {
struct WaitQueue;
} // namespace synchronization

namespace process {

using ProcessId = uint64_t;

constexpr uint32_t kMaximumImagePathLength = 63;

enum class State : uint8_t {
    New,
    Running,
    Terminated,
};

struct Process;

struct Info {
    ProcessId id;
    State state;
    uint32_t thread_count;
    uint64_t user_page_count;
    char image_path[kMaximumImagePathLength + 1];
};

/** Create a process with a private address space and no threads. */
Process* create();

/** Create a process and associate it with an existing parent for waitpid(). */
Process* create(Process* parent);

/**
 * Destroy a process after all of its threads have detached. The process address space
 * must not be active; the kernel heap and virtual-memory subsystem must be initialized.
 */
bool destroy(Process* process);

/** Return the stable process identifier, or zero for a null process. */
ProcessId id(const Process* process);

/** Return the process lifecycle state, or State::Terminated for a null process. */
State state(const Process* process);

/** Return the process's owned address space, or nullptr for a null process. */
virtual_memory::AddressSpace* address_space(Process* process);

/** Return the number of currently associated threads, or zero for a null process. */
uint32_t thread_count(const Process* process);

/** Return the process exit status, or zero for a null or still-running process. */
int64_t exit_status(const Process* process);

/** Copy a stable snapshot of the process at zero-based index, or return false at the end. */
bool info(uint64_t index, Info* output);

} // namespace process
