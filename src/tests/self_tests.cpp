#include "self_tests.hpp"

#include "context.hpp"
#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "memory.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#include "serial.hpp"
#include "thread.hpp"
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
constexpr uint64_t kContextStackSize = 4096;
constexpr uint64_t kThreadStackSize = 8192;
constexpr uint64_t kPreemptionDurationTicks = 15;

struct ContextTestState {
    context::CpuContext main_context;
    context::CpuContext thread_context;
    volatile uint64_t stage;
};

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

    // Queue all participants as one transaction so the first scheduler decision cannot
    // observe an incomplete test population or start a thread during queue mutation.
    interrupts::disable();
    const bool queued = scheduler::enqueue(state.first) && scheduler::enqueue(state.second) &&
                        scheduler::enqueue(preemption_first) && scheduler::enqueue(preemption_second);
    interrupts::enable();
    if(!queued) {
        panic::halt("scheduler smoke test could not queue its threads");
    }
}

} // namespace

namespace self_tests {

void run(uintptr_t hhdm_offset) {
    test_physical_memory();
    virtual_memory::initialize(hhdm_offset);
    test_virtual_memory();
    test_process_address_spaces();
    // The heap is initialized after page-table tests because its backing mappings depend
    // on the virtual-memory and physical-page allocators being ready first.
    kernel_heap::initialize();
    test_kernel_heap();
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
