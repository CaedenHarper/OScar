#include "spinlock.hpp"

#include "interrupts.hpp"

namespace synchronization {

void initialize(Spinlock* lock) {
    if(lock != nullptr) {
        lock->locked = 0;
    }
}

interrupts::State lock(Spinlock* lock) {
    const interrupts::State previous_state = interrupts::save_and_disable();
    while(__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        asm volatile("pause" : : : "memory");
    }
    return previous_state;
}

bool try_lock(Spinlock* lock, interrupts::State* previous_state) {
    if(lock == nullptr || previous_state == nullptr) {
        return false;
    }

    const interrupts::State saved_state = interrupts::save_and_disable();
    if(__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        interrupts::restore(saved_state);
        return false;
    }

    *previous_state = saved_state;
    return true;
}

void unlock(Spinlock* lock, interrupts::State previous_state) {
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
    interrupts::restore(previous_state);
}

} // namespace synchronization
