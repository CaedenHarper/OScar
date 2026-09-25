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
// NOLINTEND(hicpp-no-assembler, bugprone-easily-swappable-parameters)

} // namespace io
