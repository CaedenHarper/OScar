#include "mutex.hpp"

#include "interrupts.hpp"
#include "scheduler.hpp"
#include "wait_queue.hpp"

namespace synchronization {

void initialize(Mutex* mutex) {
    if(mutex == nullptr) {
        return;
    }
    mutex->locked = false;
    mutex->owner = nullptr;
    initialize(&mutex->waiters);
}

bool lock(Mutex* mutex) {
    if(mutex == nullptr) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    for(;;) {
        auto* current_thread = scheduler::current();
        if(current_thread == nullptr || current_thread == scheduler::idle()) {
            interrupts::restore(previous_state);
            return false;
        }

        if(!mutex->locked) {
            mutex->locked = true;
            mutex->owner = current_thread;
            interrupts::restore(previous_state);
            return true;
        }

        // block_current leaves interrupts disabled when this thread is resumed, allowing
        // the ownership check to restart without a wake-up race.
        if(!scheduler::block_current(&mutex->waiters)) {
            interrupts::restore(previous_state);
            return false;
        }
    }
}

bool try_lock(Mutex* mutex) {
    if(mutex == nullptr) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    auto* current_thread = scheduler::current();
    if(current_thread == nullptr || current_thread == scheduler::idle() || mutex->locked) {
        interrupts::restore(previous_state);
        return false;
    }

    mutex->locked = true;
    mutex->owner = current_thread;
    interrupts::restore(previous_state);
    return true;
}

bool unlock(Mutex* mutex) {
    if(mutex == nullptr) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    if(!mutex->locked || mutex->owner != scheduler::current()) {
        interrupts::restore(previous_state);
        return false;
    }

    mutex->locked = false;
    mutex->owner = nullptr;
    scheduler::wake_one(&mutex->waiters);
    interrupts::restore(previous_state);
    return true;
}

} // namespace synchronization
