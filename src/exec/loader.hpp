#pragma once

#include <stdint.h>

namespace kernel_thread {
struct Thread;
} // namespace kernel_thread

namespace process {
struct Process;
} // namespace process

namespace loader {

/**
 * Load an ELF64 executable into a new process and create its initial user thread.
 * The returned thread is Ready but is not enqueued; the caller owns scheduler admission.
 * The image must remain available only for the duration of this call.
 */
bool load(
    const void* image,
    uint64_t image_size,
    process::Process** output_process,
    kernel_thread::Thread** output_thread,
    process::Process* parent
);

/** Load a read-only VFS executable into a new child process and initial user thread. */
bool load_path(
    const char* path,
    process::Process* parent,
    process::Process** output_process,
    kernel_thread::Thread** output_thread
);

} // namespace loader
