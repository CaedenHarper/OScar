#include "serial.hpp"

namespace {

constexpr uint16_t kCom1 = 0x3f8;

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

} // namespace

namespace serial {

void initialize() {
    outb(kCom1 + 1, 0x00); // Disable interrupts.
    outb(kCom1 + 3, 0x80); // Enable divisor latch.
    outb(kCom1 + 0, 0x03); // 38400 baud divisor, low byte.
    outb(kCom1 + 1, 0x00); // Divisor, high byte.
    outb(kCom1 + 3, 0x03); // 8 data bits, no parity, one stop bit.
    outb(kCom1 + 2, 0xc7); // Enable and clear the FIFO.
    outb(kCom1 + 4, 0x0b); // Enable IRQs and mark the terminal ready.
}

void putc(char character) {
    if(character == '\n') {
        putc('\r');
    }

    while((inb(kCom1 + 5) & 0x20) == 0) {
        asm volatile("pause");
    }
    outb(kCom1, static_cast<uint8_t>(character));
}

void write(const char* text) {
    while(*text != '\0') {
        putc(*text++);
    }
}

void write_u64(uint64_t value) {
    char digits[20];
    unsigned length = 0;

    do {
        digits[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while(value != 0);

    while(length != 0) {
        putc(digits[--length]);
    }
}

void write_hex(uint64_t value) {
    constexpr char kDigits[] = "0123456789abcdef";
    write("0x");
    for(int shift = 60; shift >= 0; shift -= 4) {
        putc(kDigits[(value >> shift) & 0xf]);
    }
}

} // namespace serial
