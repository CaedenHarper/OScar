#include "self_tests.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "timer.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace self_tests {

void run(uintptr_t hhdm_offset) {
    self_tests_detail::test_spinlock();
    self_tests_detail::test_physical_memory();
    virtual_memory::initialize(hhdm_offset);
    self_tests_detail::test_virtual_memory();
    self_tests_detail::test_block_device();
    self_tests_detail::test_filesystem();
    self_tests_detail::test_vfs();
    self_tests_detail::test_writable_filesystem();
    self_tests_detail::test_process_address_spaces();
    // The heap is initialized after page-table tests because its backing mappings depend
    // on the virtual-memory and physical-page allocators being ready first.
    kernel_heap::initialize();
    self_tests_detail::test_kernel_heap();
    self_tests_detail::test_process_structures();
    self_tests_detail::test_malformed_elf_validation();
    if(!scheduler::initialize()) {
        panic::halt("scheduler smoke test could not initialize the scheduler");
    }
}

void run_timer() {
    if(!timer::initialize(self_tests_detail::kTimerTestFrequency)) {
        panic::halt("timer smoke test could not initialize the PIT");
    }

    // This test intentionally waits for hardware ticks rather than calling the handler,
    // ensuring the IDT, PIC/APIC routing, PIT, and interrupt-enable path work together.
    interrupts::enable();
    const uint64_t initial_ticks = timer::ticks();
    for(uint64_t loop = 0; loop < self_tests_detail::kTimerTestLoopLimit; ++loop) {
        if(timer::ticks() >= initial_ticks + self_tests_detail::kRequiredTimerTicks) {
            serial::write("Timer interrupt smoke test passed.\n");
            return;
        }
        asm volatile("pause");
    }

    panic::halt("timer smoke test did not receive timer interrupts");
}

void run_context() {
    interrupts::disable();
    self_tests_detail::test_context_switch();
    self_tests_detail::test_kernel_thread();
    interrupts::enable();
}

void run_keyboard_ps2() {
    self_tests_detail::test_keyboard_ps2();
}

void run_terminal() {
    self_tests_detail::test_terminal();
}

void run_scheduler() {
    self_tests_detail::prepare_scheduler_test();
}

bool prepare_init() {
    return self_tests_detail::prepare_init_process();
}

bool prepare_terminal() {
    return self_tests_detail::prepare_embedded_elf_thread(
        &self_tests_detail::user_program_terminal_start[0], &self_tests_detail::user_program_terminal_end[0]
    );
}

} // namespace self_tests
