#include "interrupts.hpp"
#include "mutex.hpp"
#include "panic.hpp"
#include "scheduler.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "spinlock.hpp"
#include "timer.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference)
// Synchronization and waiting-thread tests.

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

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)

} // namespace self_tests_detail
