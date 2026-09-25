#pragma once

namespace interrupts {

struct State {
    bool enabled;
};

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

} // namespace interrupts
