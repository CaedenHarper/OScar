#include "keyboard_ps2.hpp"

#include "interrupt_controller.hpp"
#include "io.hpp"
#include "keyboard.hpp"

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
constexpr uint8_t kExtendedScancode = 0xe0;
constexpr uint8_t kReleaseMask = 0x80;
constexpr uint8_t kLeftShift = 0x2a;
constexpr uint8_t kRightShift = 0x36;
constexpr uint8_t kControl = 0x1d;
constexpr uint8_t kAlt = 0x38;

bool g_available = false;
bool g_extended = false;
bool g_shift = false;
bool g_control = false;
bool g_alt = false;

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

    const char character = g_extended ? static_cast<char>(0) : translate_scancode(scancode, g_shift);
    const keyboard::Key key = character == '\n'   ? keyboard::Key::Enter
                              : character == '\b' ? keyboard::Key::Backspace
                              : character == '\t' ? keyboard::Key::Tab
                              : character == 0    ? keyboard::Key::Unknown
                                                  : keyboard::Key::Character;
    keyboard::Event event = {
        .key = key,
        .character = character,
        .pressed = pressed,
        .shift = g_shift,
        .control = g_control,
        .alt = g_alt,
    };
    g_extended = false;
    keyboard::submit_event(event);
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

    if(!keyboard::initialize()) {
        return false;
    }
    g_extended = false;
    g_shift = false;
    g_control = false;
    g_alt = false;
    g_available = true;
    return true;
}

} // namespace keyboard_ps2

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-magic-numbers, bugprone-easily-swappable-parameters)
