#pragma once

#include <stdint.h>

namespace keyboard {

enum class Key : uint8_t {
    Unknown,
    Character,
    Enter,
    Backspace,
    Tab,
};

struct Event {
    Key key;
    char character;
    bool pressed;
    bool shift;
    bool control;
    bool alt;
};

/** Initialize the hardware-independent keyboard event queue. Call before a driver submits events. */
bool initialize();

/** Return whether the generic keyboard input queue has been initialized. */
bool is_available();

/** Remove the oldest event without blocking. The caller owns the destination storage. */
bool poll(Event* event);

/** Read the oldest event, blocking the current scheduler-managed thread when the queue is empty. */
bool read(Event* event);

/** Return the number of events currently buffered by the generic input layer. */
uint32_t pending_events();

/** Submit a decoded event from a hardware keyboard driver. Safe to call from IRQ context. */
void submit_event(const Event& event);

} // namespace keyboard
