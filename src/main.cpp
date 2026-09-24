#include "idt.hpp"
#include "panic.hpp"
#include "serial.hpp"

#include <limine.h>
#include <stdint.h>

namespace {

__attribute__((used, section(".limine_requests"))) volatile uint64_t g_limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests"))) volatile limine_memmap_request g_memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests_start"))) volatile uint64_t g_limine_requests_start[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) volatile uint64_t g_limine_requests_end[] =
    LIMINE_REQUESTS_END_MARKER;

} // namespace

extern "C" [[noreturn]] void kmain() {
    serial::initialize();
    serial::write("Barebones kernel started.\n");

    if(!LIMINE_BASE_REVISION_SUPPORTED(g_limine_base_revision)) {
        panic::halt("unsupported Limine base revision");
    }

    if(g_memory_map_request.response == nullptr) {
        panic::halt("Limine did not provide a memory map");
    }

    serial::write("Memory-map entries: ");
    serial::write_u64(g_memory_map_request.response->entry_count);
    serial::write("\n");

    idt::initialize();

#if defined(OSCAR_TEST_EXCEPTION)
    serial::write("Triggering invalid-opcode exception...\n");
    asm volatile("ud2");
#endif

    panic::halt("kernel initialization complete; halting");
}
