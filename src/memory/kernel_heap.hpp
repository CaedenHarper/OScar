#pragma once

#include <stdint.h>

namespace kernel_heap {

/*
 * Initialize the kernel heap. The physical page allocator and virtual memory
 * manager must already be initialized.
 */
void initialize();

/*
 * Allocate at least size bytes from protected kernel virtual memory. Returned
 * addresses are 16-byte aligned, or nullptr if the heap cannot grow.
 */
void* allocate(uint64_t size);

/*
 * Release a block previously returned by allocate(). Returns false for a null,
 * invalid, or already freed pointer.
 */
bool free(void* pointer);

} // namespace kernel_heap
