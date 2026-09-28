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

/*
 * Verify that kernel contexts preserve their stack, instruction, and
 * callee-saved-register state across an assembly context switch.
 */
void run_context();

/** Verify that the optional PS/2 keyboard driver initializes and starts empty. */
void run_keyboard_ps2();

/** Verify that decoded keyboard events become a line-buffered terminal read. */
void run_terminal();

/**
 * Prepare cooperative round-robin scheduler smoke-test threads. The
 * scheduler must be initialized and interrupts must be enabled before this
 * function is called. Scheduling begins later when scheduler::start() runs.
 */
void run_scheduler();

/**
 * Load and enqueue the RAM-backed init ELF after kernel smoke tests are prepared.
 * The scheduler and ELF-loading prerequisites must be initialized first; scheduling
 * begins later when scheduler::start() runs.
 */
bool prepare_init();

} // namespace self_tests
