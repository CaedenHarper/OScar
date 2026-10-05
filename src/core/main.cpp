#include "framebuffer.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "interrupt_controller.hpp"
#include "interrupts.hpp"
#include "keyboard_ps2.hpp"
#include "memory.hpp"
#include "network.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#ifdef OSCAR_TEST_SUITE
#include "self_tests.hpp"
#endif
#include "serial.hpp"
#include "startup.hpp" // NOLINT(misc-include-cleaner) used only in the production boot branch.
#include "terminal.hpp"
#include "timer.hpp"

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

__attribute__((used, section(".limine_requests"))) volatile limine_framebuffer_request g_framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
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

    const limine_framebuffer* framebuffer_information = nullptr;
    if(g_framebuffer_request.response != nullptr && g_framebuffer_request.response->framebuffer_count != 0) {
        framebuffer_information = g_framebuffer_request.response->framebuffers[0];
    }
    if(framebuffer::initialize(framebuffer_information)) {
        serial::write("Framebuffer initialized: ");
        serial::write_u64(framebuffer::width());
        serial::write("x");
        serial::write_u64(framebuffer::height());
        serial::write("\n");
    } else {
        serial::write("No compatible framebuffer available; continuing without graphics.\n");
    }

#ifdef OSCAR_TEST_SUITE
    // The test profile initializes and exercises subsystems in dependency order, then
    // queues synthetic workloads before production init is allowed to run.
    self_tests::run(g_hhdm_request.response->offset);
#else
    if(!startup::initialize(g_hhdm_request.response->offset)) {
        panic::halt("could not initialize production kernel subsystems");
    }
#endif

#ifdef OSCAR_TEST_SUITE
    // The test profile initializes the raw VirtIO device in its subsystem tests;
    // initialize the protocol-facing service only after those prerequisites exist.
    if(!network::initialize()) {
        panic::halt("could not initialize the test network service");
    }
#endif

    if(!gdt::initialize()) {
        panic::halt("could not initialize the GDT and TSS");
    }

    idt::initialize();
    interrupt_controller::initialize(g_hhdm_request.response->offset);
#ifdef OSCAR_TEST_SUITE
    self_tests::run_keyboard_ps2();
#else
    if(!keyboard_ps2::initialize()) {
        panic::halt("could not initialize the PS/2 keyboard");
    }
#endif
    if(!interrupts::register_handler(interrupt_controller::kKeyboardVector, keyboard_ps2::interrupt_handler, nullptr) ||
       !interrupts::register_handler(interrupt_controller::kSerialVector, serial::interrupt_handler, nullptr) ||
       !interrupts::register_handler(interrupt_controller::kTimerVector, timer::interrupt_handler, nullptr) ||
       !interrupt_controller::route_irq(1, interrupt_controller::kKeyboardVector) ||
       !interrupt_controller::route_irq(4, interrupt_controller::kSerialVector) ||
       !interrupt_controller::route_irq(0, interrupt_controller::kTimerVector) ||
       !interrupt_controller::unmask_irq(1) || !interrupt_controller::unmask_irq(4) ||
       !interrupt_controller::unmask_irq(0)) {
        panic::halt("could not register or route interrupt handlers");
    }
    if(!terminal::initialize()) {
        panic::halt("could not initialize the kernel terminal");
    }
    serial::enable_input_interrupts();
#ifdef OSCAR_TEST_SUITE
    self_tests::run_terminal();
    self_tests::run_timer();
    self_tests::run_context();
    self_tests::run_scheduler();
#else
    if(!timer::initialize(100)) {
        panic::halt("could not initialize the PIT");
    }
#endif

    // Scheduler smoke tests are queued but do not run until this boot phase starts.
    // Returning to kmain after they terminate keeps user-space init out of the queue
    // until the kernel's startup checks have completed.
    scheduler::start_bootstrap();

    if(!network::start_service()) {
        panic::halt("could not start the network service thread");
    }

    serial::write("Exiting kernel startup.\n");
#ifdef OSCAR_TEST_SUITE
    if(!self_tests::prepare_init()) {
#else
    if(!startup::prepare_init()) {
#endif
        panic::halt("could not prepare init process");
    }

#ifdef OSCAR_TEST_EXCEPTION
    serial::write("Triggering invalid-opcode exception...\n");
    asm volatile("ud2");
#endif

    // Scheduler::start does not return: after this point execution belongs to init or
    // another thread, and the bootstrap stack is retained only as a context-switch origin.
    interrupts::enable();
    scheduler::start();
}
