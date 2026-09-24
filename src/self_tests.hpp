#pragma once

#include <stdint.h>

namespace self_tests {

/*
 * Run the kernel's boot-time subsystem smoke tests. Requires the physical
 * page allocator and virtual memory manager to be initialized first.
 */
void run(uintptr_t hhdm_offset);

/*
 * Verify that the configured timer delivers bounded progress. The IDT,
 * interrupt controller, and timer must be initialized before this function.
 */
void run_timer();

} // namespace self_tests
