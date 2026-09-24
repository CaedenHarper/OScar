#include "self_tests.hpp"

#include "kernel_heap.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "serial.hpp"
#include "virtual_memory.hpp"

namespace {

void test_physical_memory() {
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
}

void test_virtual_memory() {
    constexpr uintptr_t kTestVirtualAddress = 0x4000000000ULL;
    uintptr_t mapped_page;
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
}

void test_kernel_heap() {
    auto* first = static_cast<uint8_t*>(kernel_heap::allocate(37));
    auto* second = static_cast<uint8_t*>(kernel_heap::allocate(4097));
    if(first == nullptr || second == nullptr || first == second || (reinterpret_cast<uintptr_t>(first) % 16) != 0 ||
       (reinterpret_cast<uintptr_t>(second) % 16) != 0) {
        panic::halt("kernel heap smoke test could not allocate aligned blocks");
    }

    first[0] = 0xa5;
    second[4096] = 0x5a;
    if(first[0] != 0xa5 || second[4096] != 0x5a) {
        panic::halt("kernel heap smoke test could not access allocated blocks");
    }

    if(!kernel_heap::free(first) || !kernel_heap::free(second)) {
        panic::halt("kernel heap smoke test could not free allocated blocks");
    }
    if(kernel_heap::allocate(0) != nullptr) {
        panic::halt("kernel heap smoke test accepted a zero-sized allocation");
    }
    serial::write("Kernel heap smoke test passed.\n");
}

} // namespace

namespace self_tests {

void run(uintptr_t hhdm_offset) {
    test_physical_memory();
    virtual_memory::initialize(hhdm_offset);
    test_virtual_memory();
    kernel_heap::initialize();
    test_kernel_heap();
}

} // namespace self_tests
