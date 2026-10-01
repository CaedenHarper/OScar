#include "serial.hpp"

#include "io.hpp"
#include "keyboard.hpp"

#include <stdint.h>

// NOLINTBEGIN(bugprone-easily-swappable-parameters) private helpers here are low risk
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) serial parser state persists between IRQ bytes.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index) we
// must do pointer arithmetic and array indexing for string handling

namespace {

// Hardware
constexpr uint16_t kDataRegister = 0;
constexpr uint16_t kInterruptEnableRegister = 1;
constexpr uint16_t kFifoControlRegister = 2;
constexpr uint16_t kLineControlRegister = 3;
constexpr uint16_t kModemControlRegister = 4;
constexpr uint16_t kLineStatusRegister = 5;

constexpr uint16_t kCom1 = 0x3f8;

constexpr uint8_t kDisableInterrupts = 0x00;
constexpr uint8_t kEnableDivisorLatch = 0x80;
constexpr uint8_t kBaudDivisorLow = 0x03;
constexpr uint8_t kEightBitsNoParityOneStop = 0x03;
constexpr uint8_t kEnableAndClearFifo = 0xc7;
constexpr uint8_t kModemReady = 0x0b;
constexpr uint8_t kTransmitHoldingRegisterEmpty = 0x20;
constexpr uint8_t kReceiveDataAvailable = 0x01;
constexpr uint8_t kReceiveInterruptEnable = 0x01;
constexpr char kDeleteCharacter = static_cast<char>(0x7f);
constexpr char kEscapeCharacter = static_cast<char>(0x1b);
constexpr uint16_t kHomeSequenceParameter = 1;
constexpr uint16_t kAlternateHomeSequenceParameter = 7;
constexpr uint16_t kDeleteSequenceParameter = 3;
constexpr uint16_t kEndSequenceParameter = 4;
constexpr uint16_t kAlternateEndSequenceParameter = 8;
constexpr uint16_t kUnsupportedEscapeParameter = 0xffff;

// String formatting
constexpr unsigned kMaximumUint64DecimalDigits = 20;
constexpr uint64_t kDecimalBase = 10;

constexpr unsigned kHexHighestShift = 60;
constexpr unsigned kHexNibbleShift = 4;
constexpr uint64_t kHexNibbleMask = 0xf;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) IRQ input state persists between bytes.
bool g_previous_input_was_carriage_return = false;

enum class EscapeState : uint8_t {
    None,
    Escape,
    ControlSequence,
    FunctionSequence,
};

EscapeState g_escape_state = EscapeState::None;
uint16_t g_escape_parameter = 0;
bool g_escape_parameter_present = false;

void write_raw(char character) {
    // Polling keeps early diagnostics independent of a serial IRQ handler, which is not
    // installed yet and would otherwise introduce another boot-time dependency.
    while((io::in8(kCom1 + kLineStatusRegister) & kTransmitHoldingRegisterEmpty) == 0) {
        asm volatile("pause");
    }

    io::out8(kCom1, static_cast<uint8_t>(character));
}

keyboard::Event make_input_event(char character) {
    keyboard::Key key = keyboard::Key::Character;
    char normalized = character;
    if(character == '\r' || character == '\n') {
        key = keyboard::Key::Enter;
        normalized = '\n';
    } else if(character == '\b' || character == kDeleteCharacter) {
        key = keyboard::Key::Backspace;
        normalized = '\b';
    } else if(character == '\t') {
        key = keyboard::Key::Tab;
    }
    return {
        .key = key,
        .character = normalized,
        .pressed = true,
        .shift = false,
        .control = false,
        .alt = false,
    };
}

void submit_navigation_event(keyboard::Key key) {
    keyboard::submit_event(
        {.key = key, .character = 0, .pressed = true, .shift = false, .control = false, .alt = false}
    );
}

void reset_escape_sequence() {
    g_escape_state = EscapeState::None;
    g_escape_parameter = 0;
    g_escape_parameter_present = false;
}

keyboard::Key csi_navigation_key(char final_character) {
    if(final_character == 'A') {
        return keyboard::Key::ArrowUp;
    }
    if(final_character == 'B') {
        return keyboard::Key::ArrowDown;
    }
    if(final_character == 'C') {
        return keyboard::Key::ArrowRight;
    }
    if(final_character == 'D') {
        return keyboard::Key::ArrowLeft;
    }
    if(final_character == 'H') {
        return keyboard::Key::Home;
    }
    if(final_character == 'F') {
        return keyboard::Key::End;
    }
    if(final_character == '~' && g_escape_parameter_present) {
        if(g_escape_parameter == kHomeSequenceParameter || g_escape_parameter == kAlternateHomeSequenceParameter) {
            return keyboard::Key::Home;
        }
        if(g_escape_parameter == kDeleteSequenceParameter) {
            return keyboard::Key::Delete;
        }
        if(g_escape_parameter == kEndSequenceParameter || g_escape_parameter == kAlternateEndSequenceParameter) {
            return keyboard::Key::End;
        }
    }
    return keyboard::Key::Unknown;
}

keyboard::Key function_navigation_key(char final_character) {
    if(final_character == 'A') {
        return keyboard::Key::ArrowUp;
    }
    if(final_character == 'B') {
        return keyboard::Key::ArrowDown;
    }
    if(final_character == 'C') {
        return keyboard::Key::ArrowRight;
    }
    if(final_character == 'D') {
        return keyboard::Key::ArrowLeft;
    }
    if(final_character == 'H') {
        return keyboard::Key::Home;
    }
    if(final_character == 'F') {
        return keyboard::Key::End;
    }
    return keyboard::Key::Unknown;
}

bool consume_escape_character(char character) {
    if(g_escape_state == EscapeState::None) {
        if(character == kEscapeCharacter) {
            g_escape_state = EscapeState::Escape;
            return true;
        }
        return false;
    }

    if(g_escape_state == EscapeState::Escape) {
        if(character == '[') {
            g_escape_state = EscapeState::ControlSequence;
            return true;
        }
        if(character == 'O') {
            g_escape_state = EscapeState::FunctionSequence;
            return true;
        }
        // An isolated or unsupported escape is discarded; reprocess the current byte
        // so a normal character immediately following it is not lost.
        reset_escape_sequence();
        return false;
    }

    if(g_escape_state == EscapeState::ControlSequence && character >= '0' && character <= '9') {
        g_escape_parameter_present = true;
        g_escape_parameter = static_cast<uint16_t>((g_escape_parameter * kDecimalBase) + (character - '0'));
        return true;
    }

    if(g_escape_state == EscapeState::ControlSequence && character == ';') {
        // Modifier-bearing CSI sequences are placeholders for now. Keep consuming
        // their parameter bytes so they cannot leak into the shell as text, while
        // still recognizing the final arrow direction below.
        g_escape_parameter_present = true;
        g_escape_parameter = kUnsupportedEscapeParameter;
        return true;
    }

    const keyboard::Key key = g_escape_state == EscapeState::ControlSequence ? csi_navigation_key(character)
                                                                             : function_navigation_key(character);
    reset_escape_sequence();
    if(key != keyboard::Key::Unknown) {
        submit_navigation_event(key);
    }
    return true;
}

void submit_input_character(char character) {
    if(consume_escape_character(character)) {
        return;
    }

    // Host terminal drivers commonly send Enter as CRLF. The CR already completes
    // the line, so discard only its paired LF while preserving standalone LF input.
    if(character == '\n' && g_previous_input_was_carriage_return) {
        g_previous_input_was_carriage_return = false;
        return;
    }
    g_previous_input_was_carriage_return = character == '\r';
    keyboard::submit_event(make_input_event(character));
}

} // namespace

namespace serial {

void initialize() {
    reset_escape_sequence();
    g_previous_input_was_carriage_return = false;
    io::out8(kCom1 + kInterruptEnableRegister, kDisableInterrupts); // Disable interrupts.
    io::out8(kCom1 + kLineControlRegister, kEnableDivisorLatch); // Enable divisor latch.
    io::out8(kCom1 + kDataRegister, kBaudDivisorLow); // 38400 baud divisor, low byte.
    io::out8(kCom1 + kInterruptEnableRegister, kDisableInterrupts); // Divisor, high byte.
    io::out8(kCom1 + kLineControlRegister, kEightBitsNoParityOneStop); // 8 data bits, no parity, one stop bit.
    io::out8(kCom1 + kFifoControlRegister, kEnableAndClearFifo); // Enable and clear the FIFO.
    io::out8(kCom1 + kModemControlRegister, kModemReady); // Enable IRQs and mark the terminal ready.
}

void enable_input_interrupts() {
    // Receive IRQs stay disabled until the IDT and routing tables exist; otherwise a
    // byte arriving during early boot could vector through an uninitialized gate.
    while((io::in8(kCom1 + kLineStatusRegister) & kReceiveDataAvailable) != 0) {
        (void)io::in8(kCom1 + kDataRegister);
    }
    io::out8(kCom1 + kInterruptEnableRegister, kReceiveInterruptEnable);
}

void putc(char character) {
    // Terminals and QEMU's serial capture conventionally expect CRLF line endings.
    if(character == '\n') {
        write_raw('\r');
    }

    write_raw(character);
}

void write(const char* text) {
    while(*text != '\0') {
        putc(*text++);
    }
}

void write_u64(uint64_t value) {
    char digits[kMaximumUint64DecimalDigits];
    unsigned length = 0;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while)
    do {
        digits[length++] = static_cast<char>('0' + (value % kDecimalBase));
        value /= kDecimalBase;
    } while(value != 0);

    while(length != 0) {
        putc(digits[--length]);
    }
}

void write_hex(uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    write("0x");
    // Fixed-width output is intentional: exception diagnostics should preserve leading
    // zeroes so register values can be compared directly with debugger output.
    for(unsigned shift = kHexHighestShift;; shift -= kHexNibbleShift) {
        putc(kDigits[(value >> shift) & kHexNibbleMask]);

        // always print final nibble
        if(shift == 0) {
            break;
        }
    }
}

} // namespace serial

void serial::interrupt_handler(uint8_t vector, void* context) {
    (void)vector;
    (void)context;
    // Drain the UART rather than handling one byte per IRQ; the FIFO may contain a
    // short burst, and leaving bytes behind would retrigger the same IRQ immediately.
    while((io::in8(kCom1 + kLineStatusRegister) & kReceiveDataAvailable) != 0) {
        submit_input_character(static_cast<char>(io::in8(kCom1 + kDataRegister)));
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTEND(bugprone-easily-swappable-parameters) private helpers here are low risk
