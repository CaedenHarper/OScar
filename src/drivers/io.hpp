#pragma once

#include <stdint.h>

namespace io {

/*
 * Write an 8-bit value to an x86 I/O port. This is a privileged operation
 * intended for kernel device drivers and must not be called from user code.
 */
void out8(uint16_t port, uint8_t value);

/** Write a 16-bit value to an x86 I/O port. */
void out16(uint16_t port, uint16_t value);

/** Write a 32-bit value to an x86 I/O port. */
void out32(uint16_t port, uint32_t value);

/*
 * Read an 8-bit value from an x86 I/O port. This is a privileged operation
 * intended for kernel device drivers and must not be called from user code.
 */
uint8_t in8(uint16_t port);

/** Read a 16-bit value from an x86 I/O port. */
uint16_t in16(uint16_t port);

/** Read a 32-bit value from an x86 I/O port. */
uint32_t in32(uint16_t port);

} // namespace io
