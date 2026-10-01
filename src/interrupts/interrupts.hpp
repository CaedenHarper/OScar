#pragma once

#include <stdint.h>

namespace interrupts {

struct State {
    bool enabled;
};

using Handler = void (*)(uint8_t vector, void* context);

constexpr uint8_t kFirstExternalVector = 32;
constexpr uint8_t kLastExternalVector = 254;

/*
 * Enable maskable CPU interrupts after the IDT and interrupt-controller
 * handlers have been initialized. This function returns after executing STI.
 */
void enable();

/*
 * Disable maskable CPU interrupts before changing interrupt-sensitive state.
 * This function returns after executing CLI.
 */
void disable();

/** Save the current interrupt-enable state and disable maskable interrupts. */
State save_and_disable();

/** Restore a state returned by save_and_disable(). */
void restore(State state);

/** Register one external-interrupt handler; interrupts must be disabled by the caller. */
bool register_handler(uint8_t vector, Handler handler, void* context);

/** Remove a previously registered external-interrupt handler. */
bool unregister_handler(uint8_t vector, Handler handler);

/** Allocate an unused external-interrupt vector in the generic device range. */
bool allocate_vector(uint8_t* vector);

/** Release an allocated vector that has no registered handler. */
bool release_vector(uint8_t vector);

/** Dispatch one external interrupt to its registered device handler. */
void dispatch(uint8_t vector);

} // namespace interrupts
