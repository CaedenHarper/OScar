#pragma once

#include <stdint.h>

namespace keyboard_ps2 {

struct KeyEvent {
    uint8_t scancode;
    char character;
    bool pressed;
    bool shift;
    bool control;
    bool alt;
};

/**
 * Initialize the legacy PS/2 keyboard on IRQ1. The IDT and interrupt controller must
 * be initialized first; returns false when no usable PS/2 controller responds.
 */
bool initialize();

/** Return whether the PS/2 keyboard initialized successfully. */
bool is_available();

/**
 * Remove the oldest decoded event without blocking. Returns false when no event is queued.
 * The caller owns the destination event storage.
 */
bool poll(KeyEvent* event);

/**
 * Read the oldest decoded event, blocking the current scheduler-managed thread when the
 * queue is empty. Returns false for invalid storage, unavailable hardware, or an
 * unschedulable caller.
 */
bool read(KeyEvent* event);

/** Return the number of decoded events currently buffered. */
uint32_t pending_events();

} // namespace keyboard_ps2
