#include "keyboard_ps2.hpp"

#include "interrupt_controller.hpp"
#include "interrupts.hpp"
#include "io.hpp"
#include "scheduler.hpp"
#include "spinlock.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             readability-magic-numbers, bugprone-easily-swappable-parameters)

namespace {

constexpr uint16_t kDataPort = 0x60;
constexpr uint16_t kStatusPort = 0x64;
constexpr uint8_t kOutputBufferFull = 1U << 0U;
constexpr uint8_t kInputBufferFull = 1U << 1U;
constexpr uint8_t kControllerEnableFirstPort = 0xae;
constexpr uint8_t kControllerReadCommandByte = 0x20;
constexpr uint8_t kControllerWriteCommandByte = 0x60;
constexpr uint8_t kCommandByteTranslation = 1U << 6U;
constexpr uint8_t kCommandByteFirstPortInterrupt = 1U << 0U;
constexpr uint32_t kControllerPollLimit = 100000;
constexpr uint32_t kEventCapacity = 128;
constexpr uint8_t kExtendedScancode = 0xe0;
constexpr uint8_t kReleaseMask = 0x80;
constexpr uint8_t kLeftShift = 0x2a;
constexpr uint8_t kRightShift = 0x36;
constexpr uint8_t kControl = 0x1d;
constexpr uint8_t kAlt = 0x38;

keyboard_ps2::KeyEvent g_events[kEventCapacity];
uint32_t g_event_head = 0;
uint32_t g_event_tail = 0;
uint32_t g_event_count = 0;
bool g_available = false;
bool g_extended = false;
bool g_shift = false;
bool g_control = false;
bool g_alt = false;
synchronization::Spinlock g_event_lock = {};
synchronization::WaitQueue g_waiters = {};

bool wait_input_clear() {
    for(uint32_t attempt = 0; attempt < kControllerPollLimit; ++attempt) {
        if((io::in8(kStatusPort) & kInputBufferFull) == 0) {
            return true;
        }
        asm volatile("pause");
    }
    return false;
}

bool wait_output_full() {
    for(uint32_t attempt = 0; attempt < kControllerPollLimit; ++attempt) {
        if((io::in8(kStatusPort) & kOutputBufferFull) != 0) {
            return true;
        }
        asm volatile("pause");
    }
    return false;
}

bool send_controller_command(uint8_t command) {
    if(!wait_input_clear()) {
        return false;
    }
    io::out8(kStatusPort, command);
    return true;
}

bool send_controller_data(uint8_t value) {
    if(!wait_input_clear()) {
        return false;
    }
    io::out8(kDataPort, value);
    return true;
}

char translate_scancode(uint8_t scancode, bool shift) {
    if(scancode == 0x0e) {
        return '\b';
    }
    if(scancode == 0x0f) {
        return '\t';
    }
    if(scancode == 0x1c) {
        return '\n';
    }
    constexpr char kUnshifted[] = {
        0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,   'q', 'w', 'e',  'r', 't', 'y',
        'u', 'i', 'o', 'p', '[', ']', 0,   0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,    0,   0,   0,
    };
    constexpr char kShifted[] = {
        0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,   0,   'Q', 'W', 'E', 'R', 'T', 'Y',
        'U', 'I', 'O', 'P', '{', '}', 0,   0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,
    };
    if(scancode >= sizeof(kUnshifted) / sizeof(kUnshifted[0])) {
        return 0;
    }
    return shift ? kShifted[scancode] : kUnshifted[scancode];
}

void queue_event(const keyboard_ps2::KeyEvent& event) {
    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    if(g_event_count == kEventCapacity) {
        // Dropping the oldest event preserves fresh keyboard input and keeps the IRQ path
        // bounded; a stalled reader must not prevent future key releases from arriving.
        g_event_head = (g_event_head + 1) % kEventCapacity;
        --g_event_count;
    }
    g_events[g_event_tail] = event;
    g_event_tail = (g_event_tail + 1) % kEventCapacity;
    ++g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);

    (void)scheduler::wake_one(&g_waiters);
}

void decode_scancode(uint8_t raw_scancode) {
    if(raw_scancode == kExtendedScancode) {
        g_extended = true;
        return;
    }

    const bool pressed = (raw_scancode & kReleaseMask) == 0;
    const uint8_t scancode = raw_scancode & ~kReleaseMask;
    if(scancode == kLeftShift || scancode == kRightShift) {
        g_shift = pressed;
    } else if(scancode == kControl) {
        g_control = pressed;
    } else if(scancode == kAlt) {
        g_alt = pressed;
    }

    const uint8_t event_scancode =
        g_extended ? static_cast<uint8_t>(scancode | kExtendedScancode | (pressed ? 0 : kReleaseMask)) : raw_scancode;
    keyboard_ps2::KeyEvent event = {
        .scancode = event_scancode,
        .character = g_extended ? static_cast<char>(0) : translate_scancode(scancode, g_shift),
        .pressed = pressed,
        .shift = g_shift,
        .control = g_control,
        .alt = g_alt,
    };
    g_extended = false;
    queue_event(event);
}

} // namespace

extern "C" void keyboard_ps2_irq_handler() {
    if((io::in8(kStatusPort) & kOutputBufferFull) != 0) {
        decode_scancode(io::in8(kDataPort));
    }
    interrupt_controller::end_of_interrupt(1);
}

namespace keyboard_ps2 {

bool initialize() {
    if(g_available) {
        return true;
    }
    while((io::in8(kStatusPort) & kOutputBufferFull) != 0) {
        (void)io::in8(kDataPort);
    }
    if(!send_controller_command(kControllerEnableFirstPort) || !send_controller_command(kControllerReadCommandByte) ||
       !wait_output_full()) {
        return false;
    }
    const uint8_t command_byte = io::in8(kDataPort);
    if(!send_controller_command(kControllerWriteCommandByte) ||
       !send_controller_data(
           static_cast<uint8_t>(command_byte | kCommandByteFirstPortInterrupt | kCommandByteTranslation)
       )) {
        return false;
    }
    while((io::in8(kStatusPort) & kOutputBufferFull) != 0) {
        (void)io::in8(kDataPort);
    }

    synchronization::initialize(&g_event_lock);
    synchronization::initialize(&g_waiters);
    g_event_head = 0;
    g_event_tail = 0;
    g_event_count = 0;
    g_extended = false;
    g_shift = false;
    g_control = false;
    g_alt = false;
    g_available = true;
    return true;
}

bool is_available() {
    return g_available;
}

bool poll(KeyEvent* event) {
    if(!g_available || event == nullptr) {
        return false;
    }
    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    if(g_event_count == 0) {
        synchronization::unlock(&g_event_lock, previous_state);
        return false;
    }
    *event = g_events[g_event_head];
    g_event_head = (g_event_head + 1) % kEventCapacity;
    --g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);
    return true;
}

bool read(KeyEvent* event) {
    if(!g_available || event == nullptr) {
        return false;
    }
    for(;;) {
        if(poll(event)) {
            return true;
        }
        const interrupts::State previous_state = interrupts::save_and_disable();
        if(poll(event)) {
            interrupts::restore(previous_state);
            return true;
        }
        // Keep interrupts disabled between releasing the queue lock and enqueueing the
        // waiter, preventing a keyboard IRQ from arriving in the lost-wakeup window.
        const bool blocked = scheduler::block_current(&g_waiters);
        interrupts::restore(previous_state);
        if(!blocked) {
            return false;
        }
    }
}

uint32_t pending_events() {
    if(!g_available) {
        return 0;
    }
    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    const uint32_t count = g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);
    return count;
}

} // namespace keyboard_ps2

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-magic-numbers, bugprone-easily-swappable-parameters)
