#pragma once

#include "interrupts.hpp"

#include <stdint.h>

namespace synchronization {

struct Spinlock {
    volatile uint8_t locked;
};

/** Initialize an unlocked spinlock without allocating memory. */
void initialize(Spinlock* lock);

/**
 * Acquire a spinlock and disable local interrupts. The returned state must be passed
 * to unlock(); the lock must be held only for short, non-blocking critical sections.
 */
interrupts::State lock(Spinlock* lock);

/** Try to acquire a spinlock, returning false without changing interrupt state on failure. */
bool try_lock(Spinlock* lock, interrupts::State* previous_state);

/** Release a spinlock and restore the interrupt state returned by lock(). */
void unlock(Spinlock* lock, interrupts::State previous_state);

} // namespace synchronization
