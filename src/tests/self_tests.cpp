#include "self_tests.hpp"

#include "context.hpp"
#include "elf.hpp"
#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "loader.hpp"
#include "memory.hpp"
#include "mutex.hpp"
#include "panic.hpp"
#include "process.hpp"
#include "scheduler.hpp"
#include "serial.hpp"
#include "spinlock.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference) we must do pointer arithmetic
// for memory smoke tests

namespace {

extern "C" const uint8_t user_program_start[];
extern "C" const uint8_t user_program_end[];
extern "C" const uint8_t user_program_prime_start[];
extern "C" const uint8_t user_program_prime_end[];
extern "C" const uint8_t user_program_second_start[];
extern "C" const uint8_t user_program_second_end[];
extern "C" const uint8_t user_program_divzero_start[];
extern "C" const uint8_t user_program_divzero_end[];
extern "C" const uint8_t user_program_kernel_access_start[];
extern "C" const uint8_t user_program_kernel_access_end[];
extern "C" const uint8_t user_program_invalid_opcode_start[];
extern "C" const uint8_t user_program_invalid_opcode_end[];

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
constexpr uint64_t kContextStackSize = 4096;
constexpr uint64_t kThreadStackSize = 8192;
constexpr uint64_t kPreemptionDurationTicks = 15;
constexpr uint64_t kSleepTestDurationTicks = 2;
constexpr uintptr_t kUserTestCodeAddress = 0x400000;
constexpr uintptr_t kUserTestMessageAddress = kUserTestCodeAddress + virtual_memory::kPageSize;
constexpr uintptr_t kUserTestStackAddress = kUserTestMessageAddress + virtual_memory::kPageSize;

struct ContextTestState {
    context::CpuContext main_context;
    context::CpuContext thread_context;
    volatile uint64_t stage;
};

void thread_test_entry(void* argument);

void context_test_entry(void* argument) {
    auto* state = static_cast<ContextTestState*>(argument);
    state->stage = 1;
    context::switch_context(&state->thread_context, &state->main_context);
    state->stage = 2;
    context::switch_context(&state->thread_context, &state->main_context);
    __builtin_unreachable();
}

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

void test_context_switch() {
    static uint8_t thread_stack[kContextStackSize];
    ContextTestState state;
    state.stage = 0;
    if(!context::initialize(
           &state.thread_context,
           reinterpret_cast<uintptr_t>(thread_stack) + kContextStackSize,
           context_test_entry,
           &state
       )) {
        panic::halt("context smoke test could not initialize a thread context");
    }

    // Each switch resumes at the instruction after the previous switch call, proving that
    // the saved callee-saved registers and instruction pointer belong to the right context.
    context::switch_context(&state.main_context, &state.thread_context);
    if(state.stage != 1) {
        panic::halt("context smoke test did not enter the thread context");
    }
    context::switch_context(&state.main_context, &state.thread_context);
    if(state.stage != 2) {
        panic::halt("context smoke test did not resume the thread context");
    }
    serial::write("Kernel context switch smoke test passed.\n");
}

struct ThreadTestState {
    uint64_t marker;
};

void thread_test_entry(void* argument) {
    auto* state = static_cast<ThreadTestState*>(argument);
    state->marker = 0x4f53636172544852ULL;
}

void test_kernel_thread() {
    ThreadTestState first_state = {};
    ThreadTestState second_state = {};
    auto* first = kernel_thread::create(thread_test_entry, &first_state, kThreadStackSize);
    auto* second = kernel_thread::create(thread_test_entry, &second_state, kThreadStackSize);
    // Compare bounds before running either thread: independent stacks are an ownership
    // guarantee, not merely an implementation detail of the allocator.
    const bool stacks_overlap = kernel_thread::stack_bottom(first) < kernel_thread::stack_top(second) &&
                                kernel_thread::stack_bottom(second) < kernel_thread::stack_top(first);
    if(first == nullptr || second == nullptr || kernel_thread::id(first) == 0 ||
       kernel_thread::id(first) == kernel_thread::id(second) ||
       kernel_thread::state(first) != kernel_thread::State::Ready ||
       kernel_thread::state(second) != kernel_thread::State::Ready ||
       kernel_thread::stack_top(first) - kernel_thread::stack_bottom(first) != kThreadStackSize ||
       kernel_thread::stack_top(second) - kernel_thread::stack_bottom(second) != kThreadStackSize || stacks_overlap) {
        panic::halt("kernel thread smoke test could not create independent threads");
    }

    context::CpuContext main_context = {};
    if(!kernel_thread::run(first, &main_context) || !kernel_thread::run(second, &main_context) ||
       first_state.marker != 0x4f53636172544852ULL || second_state.marker != 0x4f53636172544852ULL ||
       kernel_thread::state(first) != kernel_thread::State::Terminated ||
       kernel_thread::state(second) != kernel_thread::State::Terminated || !kernel_thread::destroy(first) ||
       !kernel_thread::destroy(second)) {
        panic::halt("kernel thread smoke test did not complete its bootstrap path");
    }
    serial::write("Kernel thread stack and lifecycle smoke test passed.\n");
}

struct SchedulerTestState {
    kernel_thread::Thread* first;
    kernel_thread::Thread* second;
    volatile uint64_t count;
    volatile uint64_t order[4];
};

struct SchedulerTestArgument {
    SchedulerTestState* state;
    uint64_t id;
};

void record_scheduler_turn(SchedulerTestState* state, uint64_t id) {
    if(state->count >= 4) {
        panic::halt("scheduler smoke test recorded too many thread turns");
    }
    const uint64_t index = state->count;
    state->order[index] = id;
    state->count = index + 1;
}

void scheduler_test_entry(void* argument) {
    auto* test_argument = static_cast<SchedulerTestArgument*>(argument);
    auto* state = test_argument->state;
    const uint64_t id = test_argument->id;
    record_scheduler_turn(state, id);
    scheduler::yield();

    if(id == 1) {
        if(state->count != 2 || state->order[0] != 1 || state->order[1] != 2) {
            panic::halt("scheduler smoke test violated round-robin ordering");
        }
        record_scheduler_turn(state, id);
        scheduler::yield();
    } else {
        if(state->count != 3 || state->order[2] != 1) {
            panic::halt("scheduler smoke test violated round-robin ordering");
        }
        record_scheduler_turn(state, id);
        scheduler::yield();
        if(kernel_thread::state(state->first) != kernel_thread::State::Terminated || state->count != 4 ||
           state->order[2] != 1 || state->order[3] != 2) {
            panic::halt("scheduler smoke test did not terminate threads in order");
        }
        serial::write("Round-robin scheduler smoke test passed.\n");
    }
}

struct PreemptionTestState {
    volatile uint8_t first_completed;
    volatile uint8_t second_started;
};

struct PreemptionTestArgument {
    PreemptionTestState* state;
    uint64_t id;
};

struct WaitingTestState {
    volatile uint8_t expired_deadline_returned;
};

void test_spinlock() {
    synchronization::Spinlock lock = {};
    synchronization::initialize(&lock);
    interrupts::State previous_state = {false};
    if(!synchronization::try_lock(&lock, &previous_state)) {
        panic::halt("spinlock smoke test could not acquire an unlocked lock");
    }
    synchronization::unlock(&lock, previous_state);

    if(!synchronization::try_lock(&lock, &previous_state)) {
        panic::halt("spinlock smoke test could not reacquire a released lock");
    }
    synchronization::unlock(&lock, previous_state);
    serial::write("Spinlock smoke test passed.\n");
}

void waiting_test_entry(void* argument) {
    auto* state = static_cast<WaitingTestState*>(argument);
    scheduler::sleep(0);
    scheduler::sleep_until(timer::ticks());
    state->expired_deadline_returned = 1;

    const uint64_t wake_tick = timer::ticks() + kSleepTestDurationTicks;
    scheduler::sleep_until(wake_tick);
    if(timer::ticks() < wake_tick || state->expired_deadline_returned == 0) {
        panic::halt("waiting-thread smoke test resumed before its deadline");
    }
    serial::write("Waiting-thread sleep smoke test passed.\n");
}

struct MutexTestState {
    synchronization::Mutex mutex;
    volatile uint8_t holder_acquired;
};

void mutex_holder_entry(void* argument) {
    auto* state = static_cast<MutexTestState*>(argument);
    if(!synchronization::lock(&state->mutex)) {
        panic::halt("mutex smoke test holder could not acquire the mutex");
    }
    state->holder_acquired = 1;
    scheduler::yield();
    if(!synchronization::unlock(&state->mutex)) {
        panic::halt("mutex smoke test holder could not release the mutex");
    }
}

void mutex_waiter_entry(void* argument) {
    auto* state = static_cast<MutexTestState*>(argument);
    if(!synchronization::lock(&state->mutex) || state->holder_acquired == 0 ||
       !synchronization::unlock(&state->mutex)) {
        panic::halt("mutex smoke test waiter did not block and acquire in order");
    }
    serial::write("Synchronization primitive smoke test passed.\n");
}

bool write_user_bytes(
    virtual_memory::AddressSpace* address_space,
    uintptr_t virtual_address,
    const uint8_t* bytes,
    uint64_t length
) {
    uintptr_t physical_address = 0;
    if(!virtual_memory::translate(address_space, virtual_address, &physical_address)) {
        return false;
    }
    auto* destination = static_cast<uint8_t*>(virtual_memory::direct_map(physical_address));
    const uint64_t offset = physical_address % virtual_memory::kPageSize;
    if(offset + length > virtual_memory::kPageSize) {
        return false;
    }
    for(uint64_t index = 0; index < length; ++index) {
        destination[index] = bytes[index];
    }
    return true;
}

bool prepare_user_test_thread() {
    constexpr char kMessage[] = "User-mode syscall smoke test passed.\n";
    auto* user_process = process::create();
    auto* address_space = user_process == nullptr ? nullptr : process::address_space(user_process);
    if(address_space == nullptr || !virtual_memory::map_user_page(address_space, kUserTestCodeAddress, 0) ||
       !virtual_memory::map_user_page(
           address_space, kUserTestMessageAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       ) ||
       !virtual_memory::map_user_page(
           address_space, kUserTestStackAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       )) {
        return false;
    }

    uint8_t code[] = {
        0x48,
        0xb8,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x48,
        0xbf,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0x48,
        0xbe,
        sizeof(kMessage) - 1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0xcd,
        0x80,
        0x48,
        0xb8,
        0x01,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0xcd,
        0x80,
        0xeb,
        0xfe,
    };
    const uint64_t message_address = kUserTestMessageAddress;
    for(unsigned index = 0; index < sizeof(message_address); ++index) {
        code[12 + index] = static_cast<uint8_t>(message_address >> (index * 8U));
    }
    if(!write_user_bytes(address_space, kUserTestCodeAddress, code, sizeof(code)) ||
       !write_user_bytes(
           address_space, kUserTestMessageAddress, reinterpret_cast<const uint8_t*>(kMessage), sizeof(kMessage) - 1
       )) {
        return false;
    }

    auto* user_thread = kernel_thread::create_user(
        user_process, kUserTestCodeAddress, kUserTestStackAddress + virtual_memory::kPageSize
    );
    return user_thread != nullptr && scheduler::enqueue(user_thread);
}

bool prepare_elf_test_thread() {
    constexpr uintptr_t kEntry = 0x400100;
    constexpr uintptr_t kMessageAddress = kEntry + 0x40;
    constexpr uint64_t kProgramOffset = 0x100;
    constexpr uint64_t kMessageOffset = 0x40;
    constexpr char kMessage[] = "ELF loader smoke test passed.\n";
    constexpr uint64_t kImageSize = 512;
    alignas(8) static uint8_t image[kImageSize] = {};

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
    header->entry = kEntry;
    header->program_header_offset = sizeof(elf::Header);
    header->header_size = sizeof(elf::Header);
    header->program_header_size = sizeof(elf::ProgramHeader);
    header->program_header_count = 1;

    auto* program_header = reinterpret_cast<elf::ProgramHeader*>(image + sizeof(elf::Header));
    program_header->type = elf::kLoad;
    program_header->flags = elf::kReadable | elf::kExecutableFlag;
    program_header->offset = kProgramOffset;
    program_header->virtual_address = kEntry;
    program_header->file_size = kMessageOffset + sizeof(kMessage) - 1;
    program_header->memory_size = virtual_memory::kPageSize;
    program_header->alignment = virtual_memory::kPageSize;

    uint8_t code[] = {
        0xb8, 0, 0, 0,    0,    0x48, 0xbf, 0, 0, 0, 0,    0,    0,    0,    0, 0xbe, sizeof(kMessage) - 1,
        0,    0, 0, 0xcd, 0x80, 0xb8, 0x01, 0, 0, 0, 0xcd, 0x80, 0xeb, 0xfe,
    };
    for(uint64_t index = 0; index < sizeof(code); ++index) {
        image[kProgramOffset + index] = code[index];
    }
    for(uint64_t index = 0; index < sizeof(kMessageAddress); ++index) {
        image[kProgramOffset + 7 + index] = static_cast<uint8_t>(kMessageAddress >> (index * 8U));
    }
    for(uint64_t index = 0; index < sizeof(kMessage) - 1; ++index) {
        image[kProgramOffset + kMessageOffset + index] = static_cast<uint8_t>(kMessage[index]);
    }

    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    if(!loader::load(image, sizeof(image), &process, &thread) || process == nullptr || thread == nullptr ||
       !scheduler::enqueue(thread)) {
        return false;
    }
    serial::write("ELF loader process prepared.\n");
    return true;
}

bool prepare_real_elf_test_thread() {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(user_program_end - user_program_start);
    if(!loader::load(user_program_start, image_size, &process, &thread) || process == nullptr || thread == nullptr ||
       !scheduler::enqueue(thread)) {
        return false;
    }
    serial::write("Real ELF process prepared.\n");
    return true;
}

bool prepare_embedded_elf_thread(const uint8_t* image, const uint8_t* image_end) {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(image_end - image);
    if(!loader::load(image, image_size, &process, &thread) || process == nullptr || thread == nullptr) {
        return false;
    }
    return scheduler::enqueue(thread);
}

bool prepare_crash_test_thread(const uint8_t* image, const uint8_t* image_end) {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(image_end - image);
    if(!loader::load(image, image_size, &process, &thread) || process == nullptr || thread == nullptr) {
        return false;
    }
    return scheduler::enqueue(thread);
}

void preemption_test_entry(void* argument) {
    auto* test_argument = static_cast<PreemptionTestArgument*>(argument);
    auto* state = test_argument->state;
    const uint64_t start_tick = timer::ticks();
    if(test_argument->id == 2) {
        if(state->first_completed != 0) {
            panic::halt("timer scheduler smoke test did not preempt the first thread");
        }
        state->second_started = 1;
    }

    while(timer::ticks() < start_tick + kPreemptionDurationTicks) {
        asm volatile("pause");
    }

    if(test_argument->id == 1) {
        state->first_completed = 1;
    } else if(state->second_started == 0) {
        panic::halt("timer scheduler smoke test did not start the second thread");
    } else {
        serial::write("Timer preemption smoke test passed.\n");
    }
}

void prepare_scheduler_test() {
    static SchedulerTestState state = {};
    static SchedulerTestArgument first_argument = {&state, 1};
    static SchedulerTestArgument second_argument = {&state, 2};
    static PreemptionTestState preemption_state = {};
    static PreemptionTestArgument preemption_first_argument = {&preemption_state, 1};
    static PreemptionTestArgument preemption_second_argument = {&preemption_state, 2};
    static WaitingTestState waiting_state = {};
    static MutexTestState mutex_state = {};

    state.first = kernel_thread::create(scheduler_test_entry, &first_argument);
    state.second = kernel_thread::create(scheduler_test_entry, &second_argument);
    if(state.first == nullptr || state.second == nullptr) {
        panic::halt("scheduler smoke test could not create its threads");
    }

    auto* preemption_first = kernel_thread::create(preemption_test_entry, &preemption_first_argument);
    auto* preemption_second = kernel_thread::create(preemption_test_entry, &preemption_second_argument);
    if(preemption_first == nullptr || preemption_second == nullptr) {
        panic::halt("timer scheduler smoke test could not create its threads");
    }

    auto* waiting_thread = kernel_thread::create(waiting_test_entry, &waiting_state);
    if(waiting_thread == nullptr) {
        panic::halt("waiting-thread smoke test could not create its thread");
    }

    synchronization::initialize(&mutex_state.mutex);
    auto* mutex_holder = kernel_thread::create(mutex_holder_entry, &mutex_state);
    auto* mutex_waiter = kernel_thread::create(mutex_waiter_entry, &mutex_state);
    if(mutex_holder == nullptr || mutex_waiter == nullptr) {
        panic::halt("mutex smoke test could not create its threads");
    }

    if(!prepare_user_test_thread()) {
        panic::halt("user-mode smoke test could not prepare its process");
    }
    if(!prepare_elf_test_thread()) {
        panic::halt("ELF loader smoke test could not prepare its process");
    }
    if(!prepare_real_elf_test_thread()) {
        panic::halt("real ELF executable smoke test could not prepare its process");
    }
    if(!prepare_embedded_elf_thread(user_program_prime_start, user_program_prime_end) ||
       !prepare_embedded_elf_thread(user_program_second_start, user_program_second_end)) {
        panic::halt("multiple ELF executable smoke tests could not prepare their processes");
    }
    if(!prepare_crash_test_thread(user_program_divzero_start, user_program_divzero_end) ||
       !prepare_crash_test_thread(user_program_kernel_access_start, user_program_kernel_access_end) ||
       !prepare_crash_test_thread(user_program_invalid_opcode_start, user_program_invalid_opcode_end)) {
        panic::halt("crash ELF smoke tests could not prepare their processes");
    }

    // Queue all participants as one transaction so the first scheduler decision cannot
    // observe an incomplete test population or start a thread during queue mutation.
    interrupts::disable();
    const bool queued = scheduler::enqueue(state.first) && scheduler::enqueue(state.second) &&
                        scheduler::enqueue(preemption_first) && scheduler::enqueue(preemption_second) &&
                        scheduler::enqueue(waiting_thread) && scheduler::enqueue(mutex_holder) &&
                        scheduler::enqueue(mutex_waiter);
    interrupts::enable();
    if(!queued) {
        panic::halt("scheduler smoke test could not queue its threads");
    }
}

} // namespace

namespace self_tests {

void run(uintptr_t hhdm_offset) {
    test_spinlock();
    test_physical_memory();
    virtual_memory::initialize(hhdm_offset);
    test_virtual_memory();
    test_process_address_spaces();
    // The heap is initialized after page-table tests because its backing mappings depend
    // on the virtual-memory and physical-page allocators being ready first.
    kernel_heap::initialize();
    test_kernel_heap();
    test_process_structures();
    test_malformed_elf_validation();
    if(!scheduler::initialize()) {
        panic::halt("scheduler smoke test could not initialize the scheduler");
    }
}

void run_timer() {
    if(!timer::initialize(kTimerTestFrequency)) {
        panic::halt("timer smoke test could not initialize the PIT");
    }

    // This test intentionally waits for hardware ticks rather than calling the handler,
    // ensuring the IDT, PIC/APIC routing, PIT, and interrupt-enable path work together.
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

void run_context() {
    interrupts::disable();
    test_context_switch();
    test_kernel_thread();
    interrupts::enable();
}

void run_scheduler() {
    prepare_scheduler_test();
}

} // namespace self_tests

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)
