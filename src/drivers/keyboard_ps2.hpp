#pragma once

#include <stdint.h>

namespace keyboard_ps2 {

/**
 * Initialize the legacy PS/2 keyboard on IRQ1. The IDT and interrupt controller must
 * be initialized first; returns false when no usable PS/2 controller responds.
 */
bool initialize();

/** Return whether the PS/2 keyboard initialized successfully. */
bool is_available();

} // namespace keyboard_ps2
