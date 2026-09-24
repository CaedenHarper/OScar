#include "serial.hpp"

#include <stdint.h>

// NOLINTBEGIN(bugprone-easily-swappable-parameters) private helpers here are low risk
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

// String formatting
constexpr unsigned kMaximumUint64DecimalDigits = 20;
constexpr uint64_t kDecimalBase = 10;

constexpr unsigned kHexHighestShift = 60;
constexpr unsigned kHexNibbleShift = 4;
constexpr uint64_t kHexNibbleMask = 0xf;

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    // NOLINTNEXTLINE(misc-const-correctness) assembly writes to it, so it cannot be const
    uint8_t value = 0;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void write_raw(char character) {
    while((inb(kCom1 + kLineStatusRegister) & kTransmitHoldingRegisterEmpty) == 0) {
        asm volatile("pause");
    }

    outb(kCom1, static_cast<uint8_t>(character));
}

} // namespace

namespace serial {

void initialize() {
    outb(kCom1 + kInterruptEnableRegister, kDisableInterrupts); // Disable interrupts.
    outb(kCom1 + kLineControlRegister, kEnableDivisorLatch); // Enable divisor latch.
    outb(kCom1 + kDataRegister, kBaudDivisorLow); // 38400 baud divisor, low byte.
    outb(kCom1 + kInterruptEnableRegister, kDisableInterrupts); // Divisor, high byte.
    outb(kCom1 + kLineControlRegister, kEightBitsNoParityOneStop); // 8 data bits, no parity, one stop bit.
    outb(kCom1 + kFifoControlRegister, kEnableAndClearFifo); // Enable and clear the FIFO.
    outb(kCom1 + kModemControlRegister, kModemReady); // Enable IRQs and mark the terminal ready.
}

void putc(char character) {
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
    for(unsigned shift = kHexHighestShift;; shift -= kHexNibbleShift) {
        putc(kDigits[(value >> shift) & kHexNibbleMask]);

        // always print final nibble
        if(shift == 0) {
            break;
        }
    }
}

} // namespace serial

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(bugprone-easily-swappable-parameters) private helpers here are low risk
