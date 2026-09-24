#include "idt.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "serial.hpp"
#include "virtual_memory.hpp"

#include <limine.h>
#include <stdint.h>

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

extern "C" [[noreturn]] void kmain() {
    serial::initialize();
    serial::write("Barebones kernel started.\n");

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

    uintptr_t page_a;
    uintptr_t page_b;
    if(!physical_memory::allocate_page(&page_a) || !physical_memory::allocate_page(&page_b)) {
        panic::halt("physical page allocator could not allocate its smoke-test pages");
    }
    serial::write("Allocated physical pages: ");
    serial::write_hex(page_a);
    serial::write(", ");
    serial::write_hex(page_b);
    serial::write("\n");
    if(!physical_memory::free_page(page_a) || !physical_memory::free_page(page_b)) {
        panic::halt("physical page allocator could not free its smoke-test pages");
    }
    serial::write("Physical page allocator smoke test passed.\n");

    virtual_memory::initialize(g_hhdm_request.response->offset);
    uintptr_t mapped_page;
    constexpr uintptr_t kTestVirtualAddress = 0x4000000000ULL;
    if(!physical_memory::allocate_page(&mapped_page)) {
        panic::halt("virtual memory smoke test could not allocate a physical page");
    }
    if(!virtual_memory::map_page(kTestVirtualAddress, mapped_page, virtual_memory::kWritable)) {
        panic::halt("virtual memory smoke test could not create a mapping");
    }
    *reinterpret_cast<volatile uint64_t*>(kTestVirtualAddress) = 0x4f53636172564d4dULL;
    uintptr_t translated_page;
    if(!virtual_memory::translate(kTestVirtualAddress, &translated_page) || translated_page != mapped_page) {
        panic::halt("virtual memory smoke test translated the wrong address");
    }
    serial::write("Virtual memory mapping smoke test passed.\n");
    uintptr_t unmapped_page;
    if(!virtual_memory::unmap_page(kTestVirtualAddress, &unmapped_page) || unmapped_page != mapped_page ||
       !physical_memory::free_page(mapped_page)) {
        panic::halt("virtual memory smoke test could not tear down its mapping");
    }

    idt::initialize();

#if defined(OSCAR_TEST_EXCEPTION)
    serial::write("Triggering invalid-opcode exception...\n");
    asm volatile("ud2");
#endif

    panic::halt("kernel initialization complete; halting");
}
