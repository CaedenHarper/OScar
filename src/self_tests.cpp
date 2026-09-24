#include "self_tests.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "serial.hpp"
#include "timer.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference) we must do pointer arithmetic
// for memory smoke tests

namespace {

constexpr uint64_t kSmallAllocationSize = 37;
constexpr uint64_t kCrossPageAllocationSize = virtual_memory::kPageSize + 1;
constexpr uintptr_t kExpectedHeapAlignment = 16;
constexpr uintptr_t kFirstByteOffset = 0;
constexpr uintptr_t kPageBoundaryOffset = virtual_memory::kPageSize;
constexpr uintptr_t kFirstUserTestAddress = 0x2000000000ULL;
constexpr uintptr_t kSecondUserTestAddress = kFirstUserTestAddress + virtual_memory::kPageSize;
constexpr uintptr_t kKernelHeapAddress = 0xffff900000000000ULL;
constexpr uintptr_t kReadOnlyUserTestAddress = kSecondUserTestAddress + virtual_memory::kPageSize;

constexpr uint64_t kVirtualMemoryTestPattern = 0x4f53636172564d4dULL;
constexpr uint64_t kFirstAddressSpacePattern = 0x4f53636172415331ULL;
constexpr uint64_t kSecondAddressSpacePattern = 0x4f53636172415332ULL;
constexpr uint8_t kFirstHeapTestPattern = 0xa5;
constexpr uint8_t kSecondHeapTestPattern = 0x5a;
constexpr uint32_t kTimerTestFrequency = 100;
constexpr uint64_t kRequiredTimerTicks = 3;
constexpr uint64_t kTimerTestLoopLimit = 100000000;

void test_physical_memory() {
    uintptr_t page_a = 0;
    uintptr_t page_b = 0;
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
    uintptr_t mapped_page = 0;
    if(!physical_memory::allocate_page(&mapped_page)) {
        panic::halt("virtual memory smoke test could not allocate a physical page");
    }
    if(!virtual_memory::map_page(kTestVirtualAddress, mapped_page, virtual_memory::kWritable)) {
        panic::halt("virtual memory smoke test could not create a mapping");
    }

    *reinterpret_cast<volatile uint64_t*>(kTestVirtualAddress) = kVirtualMemoryTestPattern;

    uintptr_t translated_page = 0;
    if(!virtual_memory::translate(kTestVirtualAddress, &translated_page) || translated_page != mapped_page) {
        panic::halt("virtual memory smoke test translated the wrong address");
    }
    serial::write("Virtual memory mapping smoke test passed.\n");

    uintptr_t unmapped_page = 0;
    if(!virtual_memory::unmap_page(kTestVirtualAddress, &unmapped_page) || unmapped_page != mapped_page ||
       !physical_memory::free_page(mapped_page)) {
        panic::halt("virtual memory smoke test could not tear down its mapping");
    }
}

void test_kernel_heap() {
    auto* first = static_cast<uint8_t*>(kernel_heap::allocate(kSmallAllocationSize));
    auto* second = static_cast<uint8_t*>(kernel_heap::allocate(kCrossPageAllocationSize));
    if(first == nullptr || second == nullptr || first == second ||
       (reinterpret_cast<uintptr_t>(first) % kExpectedHeapAlignment) != 0 ||
       (reinterpret_cast<uintptr_t>(second) % kExpectedHeapAlignment) != kFirstByteOffset) {
        panic::halt("kernel heap smoke test could not allocate aligned blocks");
    }

    first[kFirstByteOffset] = kFirstHeapTestPattern;
    second[kPageBoundaryOffset] = kSecondHeapTestPattern;
    if(first[kFirstByteOffset] != kFirstHeapTestPattern || second[kPageBoundaryOffset] != kSecondHeapTestPattern) {
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

void test_process_address_spaces() {
    const uint64_t initial_free_pages = physical_memory::free_pages();
    virtual_memory::AddressSpace first_address_space = {};
    virtual_memory::AddressSpace second_address_space = {};
    if(!virtual_memory::create_address_space(&first_address_space) ||
       !virtual_memory::create_address_space(&second_address_space)) {
        panic::halt("address-space smoke test could not create address spaces");
    }

    if(!virtual_memory::map_user_page(
           &first_address_space, kFirstUserTestAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       ) ||
       !virtual_memory::map_user_page(
           &second_address_space, kFirstUserTestAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       ) ||
       !virtual_memory::map_user_page(
           &first_address_space, kSecondUserTestAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       ) ||
       !virtual_memory::map_user_page(&first_address_space, kReadOnlyUserTestAddress, virtual_memory::kNoExecute)) {
        panic::halt("address-space smoke test could not create user mappings");
    }

    if(virtual_memory::map_user_page(&first_address_space, kKernelHeapAddress, virtual_memory::kWritable)) {
        panic::halt("address-space smoke test accepted a kernel address");
    }

    if(!virtual_memory::validate_user_range(
           &first_address_space, kFirstUserTestAddress, virtual_memory::kPageSize * 2, virtual_memory::kWritable
       ) ||
       !virtual_memory::validate_user_range(
           &first_address_space, kFirstUserTestAddress + virtual_memory::kPageSize - 1, 2, virtual_memory::kWritable
       ) ||
       !virtual_memory::validate_user_range(&first_address_space, kReadOnlyUserTestAddress, 1, 0) ||
       virtual_memory::validate_user_range(
           &first_address_space, kReadOnlyUserTestAddress, 1, virtual_memory::kWritable
       ) ||
       virtual_memory::validate_user_range(
           &first_address_space, kReadOnlyUserTestAddress + virtual_memory::kPageSize, 1, 0
       ) ||
       virtual_memory::validate_user_range(&first_address_space, kKernelHeapAddress, 1, 0) ||
       virtual_memory::validate_user_range(&first_address_space, 0x00007fffffffffffULL, 2, 0)) {
        panic::halt("address-space smoke test rejected or accepted an invalid user range");
    }

    if(!virtual_memory::activate(&first_address_space)) {
        panic::halt("address-space smoke test could not activate the first address space");
    }
    auto* first_page = reinterpret_cast<volatile uint64_t*>(kFirstUserTestAddress);
    auto* second_page = reinterpret_cast<volatile uint64_t*>(kSecondUserTestAddress);
    *first_page = kFirstAddressSpacePattern;
    *second_page = kSecondAddressSpacePattern;

    if(!virtual_memory::activate(&second_address_space)) {
        panic::halt("address-space smoke test could not activate the second address space");
    }
    auto* isolated_page = reinterpret_cast<volatile uint64_t*>(kFirstUserTestAddress);
    if(*isolated_page != 0) {
        panic::halt("address-space smoke test found shared user memory");
    }
    *isolated_page = kSecondAddressSpacePattern;

    uintptr_t first_physical_address = 0;
    uintptr_t second_physical_address = 0;
    if(!virtual_memory::translate(&first_address_space, kFirstUserTestAddress, &first_physical_address) ||
       !virtual_memory::translate(&second_address_space, kFirstUserTestAddress, &second_physical_address) ||
       first_physical_address == second_physical_address) {
        panic::halt("address-space smoke test found identical physical mappings");
    }

    if(!virtual_memory::activate(virtual_memory::kernel_address_space())) {
        panic::halt("address-space smoke test could not restore the kernel address space");
    }
    virtual_memory::destroy_address_space(&first_address_space);
    virtual_memory::destroy_address_space(&second_address_space);
    if(physical_memory::free_pages() != initial_free_pages) {
        panic::halt("address-space smoke test leaked physical pages");
    }
    serial::write("Process address-space smoke test passed.\n");
}

} // namespace

namespace self_tests {

void run(uintptr_t hhdm_offset) {
    test_physical_memory();
    virtual_memory::initialize(hhdm_offset);
    test_virtual_memory();
    test_process_address_spaces();
    kernel_heap::initialize();
    test_kernel_heap();
}

void run_timer() {
    if(!timer::initialize(kTimerTestFrequency)) {
        panic::halt("timer smoke test could not initialize the PIT");
    }

    interrupts::enable();
    const uint64_t initial_ticks = timer::ticks();
    for(uint64_t loop = 0; loop < kTimerTestLoopLimit; ++loop) {
        if(timer::ticks() >= initial_ticks + kRequiredTimerTicks) {
            serial::write("Timer interrupt smoke test passed.\n");
            return;
        }
        asm volatile("pause");
    }

    panic::halt("timer smoke test did not receive timer interrupts");
}

} // namespace self_tests

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)
