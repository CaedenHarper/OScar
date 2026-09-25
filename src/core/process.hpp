#pragma once

#include <stdint.h>

namespace virtual_memory {
struct AddressSpace;
} // namespace virtual_memory

namespace process {

using ProcessId = uint64_t;

enum class State : uint8_t {
    New,
    Running,
    Terminated,
};

struct Process;

/** Create a process with a private address space and no threads. */
Process* create();

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

} // namespace process
