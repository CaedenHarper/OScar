#include "io.hpp"

#include <stdint.h>

namespace io {

// NOLINTBEGIN(hicpp-no-assembler, bugprone-easily-swappable-parameters) x86 port I/O requires inline assembly
void out8(uint16_t port, uint8_t value) {
    // The "Nd" constraint lets GCC use an immediate port for constant callers while
    // still supporting the DX register for runtime-selected ports.
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t in8(uint16_t port) {
    // NOLINTNEXTLINE(misc-const-correctness) assembly writes to this output operand
    uint8_t value = 0;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void out16(uint16_t port, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

void out32(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint16_t in16(uint16_t port) {
    // NOLINTNEXTLINE(misc-const-correctness) assembly writes to this output operand
    uint16_t value = 0;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t in32(uint16_t port) {
    // NOLINTNEXTLINE(misc-const-correctness) assembly writes to this output operand
    uint32_t value = 0;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
// NOLINTEND(hicpp-no-assembler, bugprone-easily-swappable-parameters)

} // namespace io
