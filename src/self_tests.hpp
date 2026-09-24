#pragma once

#include <stdint.h>

namespace self_tests {

/*
 * Run the kernel's boot-time subsystem smoke tests. Requires the physical
 * page allocator and virtual memory manager to be initialized first.
 */
void run(uintptr_t hhdm_offset);

} // namespace self_tests
