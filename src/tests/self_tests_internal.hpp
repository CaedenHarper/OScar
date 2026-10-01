#pragma once

#include "mutex.hpp"
#include "thread.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace self_tests_detail {

extern "C" const uint8_t user_program_prime_start[];
extern "C" const uint8_t user_program_prime_end[];
extern "C" const uint8_t user_program_second_start[];
extern "C" const uint8_t user_program_second_end[];
extern "C" const uint8_t user_program_filesystem_start[];
extern "C" const uint8_t user_program_filesystem_end[];
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

struct ThreadTestState {
    uint64_t marker;
};

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

struct MutexTestState {
    synchronization::Mutex mutex;
    volatile uint8_t holder_acquired;
};

void thread_test_entry(void* argument);
void test_context_switch();
void test_kernel_thread();
void test_physical_memory();
void test_virtual_memory();
void test_kernel_heap();
void test_process_structures();
void test_malformed_elf_validation();
void test_process_address_spaces();
void test_block_device();
void test_virtio_network();
void test_pci();
void test_ethernet();
void test_arp();
void test_ipv4();
void test_icmp();
void test_filesystem();
void test_vfs();
void test_writable_filesystem();
void test_filesystem_mutation();
void test_filesystem_edge_cases();
void test_keyboard_ps2();
void test_terminal();
void test_spinlock();
void waiting_test_entry(void* argument);
void mutex_holder_entry(void* argument);
void mutex_waiter_entry(void* argument);
void scheduler_test_entry(void* argument);
void preemption_test_entry(void* argument);
bool prepare_user_test_thread();
bool prepare_elf_test_thread();
bool prepare_real_elf_test_thread();
bool prepare_embedded_elf_thread(const uint8_t* image, const uint8_t* image_end);
bool prepare_crash_test_thread(const uint8_t* image, const uint8_t* image_end);
bool prepare_init_process();
void prepare_scheduler_test();

} // namespace self_tests_detail
