#include "interrupts.hpp"
#include "mutex.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "thread.hpp"
#include "timer.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference)
// Cooperative and preemptive scheduler test state and entries.

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
       !prepare_embedded_elf_thread(user_program_second_start, user_program_second_end) ||
       !prepare_embedded_elf_thread(user_program_filesystem_start, user_program_filesystem_end)) {
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

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)

} // namespace self_tests_detail
