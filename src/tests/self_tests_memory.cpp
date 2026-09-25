#include "elf.hpp"
#include "kernel_heap.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "process.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "thread.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference)
// Physical memory, virtual memory, heap, process, and ELF validation tests.

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

    // Touch the mapping before translating it so the test covers both page-table setup
    // and the CPU's ability to use the resulting virtual address.
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

void test_process_structures() {
    auto* first_process = process::create();
    auto* second_process = process::create();
    if(first_process == nullptr || second_process == nullptr || process::id(first_process) == 0 ||
       process::id(first_process) == process::id(second_process) ||
       process::state(first_process) != process::State::New || process::thread_count(first_process) != 0 ||
       process::address_space(first_process) == nullptr) {
        panic::halt("process smoke test could not create independent processes");
    }

    auto* first_thread = kernel_thread::create(first_process, thread_test_entry, nullptr, kThreadStackSize);
    auto* second_thread = kernel_thread::create(first_process, thread_test_entry, nullptr, kThreadStackSize);
    if(first_thread == nullptr || second_thread == nullptr ||
       process::state(first_process) != process::State::Running || process::thread_count(first_process) != 2 ||
       kernel_thread::owner_process(first_thread) != first_process ||
       kernel_thread::address_space(first_thread) != process::address_space(first_process) ||
       process::destroy(first_process)) {
        panic::halt("process smoke test could not associate threads with a process");
    }

    if(!kernel_thread::destroy(first_thread) || process::thread_count(first_process) != 1 ||
       !kernel_thread::destroy(second_thread) || process::thread_count(first_process) != 0 ||
       process::state(first_process) != process::State::Terminated || !process::destroy(first_process) ||
       !process::destroy(second_process)) {
        panic::halt("process smoke test could not release process-owned resources");
    }
    serial::write("Process structure smoke test passed.\n");
}

void initialize_minimal_elf(uint8_t* image) {
    auto* header = reinterpret_cast<elf::Header*>(image);
    header->identity[0] = 0x7f;
    header->identity[1] = 'E';
    header->identity[2] = 'L';
    header->identity[3] = 'F';
    header->identity[4] = elf::kClass64;
    header->identity[5] = elf::kLittleEndian;
    header->type = elf::kExecutable;
    header->machine = elf::kMachineX86_64;
    header->version = elf::kCurrentVersion;
    header->entry = 0x400000;
    header->program_header_offset = sizeof(elf::Header);
    header->header_size = sizeof(elf::Header);
    header->program_header_size = sizeof(elf::ProgramHeader);
    header->program_header_count = 1;

    auto* segment = reinterpret_cast<elf::ProgramHeader*>(image + sizeof(elf::Header));
    segment->type = elf::kLoad;
    segment->flags = elf::kReadable | elf::kExecutableFlag;
    segment->offset = 0;
    segment->virtual_address = 0x400000;
    segment->file_size = 1;
    segment->memory_size = virtual_memory::kPageSize;
    segment->alignment = virtual_memory::kPageSize;
}

void test_malformed_elf_validation() {
    alignas(8) uint8_t truncated[sizeof(elf::Header)] = {};
    if(elf::validate(truncated, sizeof(truncated))) {
        panic::halt("ELF validation accepted a truncated header");
    }

    alignas(8) uint8_t image[sizeof(elf::Header) + sizeof(elf::ProgramHeader)] = {};
    initialize_minimal_elf(image);
    auto* header = reinterpret_cast<elf::Header*>(image);
    auto* segment = reinterpret_cast<elf::ProgramHeader*>(image + sizeof(elf::Header));

    segment->file_size = 2;
    segment->memory_size = 1;
    if(elf::validate(image, sizeof(image))) {
        panic::halt("ELF validation accepted p_filesz larger than p_memsz");
    }

    initialize_minimal_elf(image);
    header->entry = 0x500000;
    if(elf::validate(image, sizeof(image))) {
        panic::halt("ELF validation accepted an entry point outside executable segments");
    }

    initialize_minimal_elf(image);
    segment->virtual_address = 0x00007ffffffff000ULL;
    segment->memory_size = virtual_memory::kPageSize * 2;
    if(elf::validate(image, sizeof(image))) {
        panic::halt("ELF validation accepted a segment crossing the user address limit");
    }
    serial::write("Malformed ELF validation smoke test passed.\n");
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

    // Restore the kernel root before destroying either test root; destroying the active
    // page tables would leave CR3 pointing at physical pages returned to the allocator.
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

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)

} // namespace self_tests_detail
