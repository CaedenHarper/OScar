#pragma once

namespace interrupts {

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

} // namespace interrupts
