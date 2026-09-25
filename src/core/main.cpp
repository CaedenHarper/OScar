#include "gdt.hpp"
#include "idt.hpp"
#include "interrupt_controller.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#include "self_tests.hpp"
#include "serial.hpp"

#include <limine.h>
#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) vars must be externally mutable by limine

namespace {

__attribute__((used, section(".limine_requests"))) volatile uint64_t g_limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests"))) volatile limine_memmap_request g_memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests"))) volatile limine_hhdm_request g_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests_start"))) volatile uint64_t g_limine_requests_start[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) volatile uint64_t g_limine_requests_end[] =
    LIMINE_REQUESTS_END_MARKER;

} // namespace

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" [[noreturn]] void kmain() {
    serial::initialize();
    serial::write("Barebones kernel started.\n");

    // Validate Limine responses before passing their pointers to subsystem code. This
    // keeps boot failures at the boundary instead of turning missing firmware data into
    // an unrelated page fault later in initialization.
    if(!LIMINE_BASE_REVISION_SUPPORTED(g_limine_base_revision)) {
        panic::halt("unsupported Limine base revision");
    }

    if(g_memory_map_request.response == nullptr) {
        panic::halt("Limine did not provide a memory map");
    }

    if(g_hhdm_request.response == nullptr) {
        panic::halt("Limine did not provide a higher-half direct map");
    }

    serial::write("Memory-map entries: ");
    serial::write_u64(g_memory_map_request.response->entry_count);
    serial::write("\n");

    physical_memory::initialize(g_memory_map_request.response);
    serial::write("Physical pages: ");
    serial::write_u64(physical_memory::total_pages());
    serial::write(" total, ");
    serial::write_u64(physical_memory::free_pages());
    serial::write(" free\n");

    // Memory tests must run before the IDT and scheduler take ownership of interrupts;
    // the later tests deliberately exercise those newly initialized subsystems.
    self_tests::run(g_hhdm_request.response->offset);

    if(!gdt::initialize()) {
        panic::halt("could not initialize the GDT and TSS");
    }

    idt::initialize();
    interrupt_controller::initialize(g_hhdm_request.response->offset);
    self_tests::run_timer();
    self_tests::run_context();
    self_tests::run_scheduler();

#ifdef OSCAR_TEST_EXCEPTION
    serial::write("Triggering invalid-opcode exception...\n");
    asm volatile("ud2");
#endif

    // Scheduler::start does not return: after this point execution belongs to a thread,
    // and the bootstrap stack is retained only as a context-switch origin.
    scheduler::start();
}
