#pragma once

#include <stdint.h>

namespace timer {

/*
 * Initialize the selected timer backend at the requested frequency. The
 * interrupt controller and IDT must already be initialized. Returns false if
 * the backend cannot represent the requested frequency.
 */
bool initialize(uint32_t frequency_hz);

/*
 * Return the number of timer interrupts received since initialization.
 */
uint64_t ticks();

/*
 * Return the actual frequency configured by the selected timer backend.
 */
uint32_t frequency_hz();

} // namespace timer
