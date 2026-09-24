#pragma once

#include <stdint.h>

namespace io {

/*
 * Write an 8-bit value to an x86 I/O port. This is a privileged operation
 * intended for kernel device drivers and must not be called from user code.
 */
void out8(uint16_t port, uint8_t value);

/*
 * Read an 8-bit value from an x86 I/O port. This is a privileged operation
 * intended for kernel device drivers and must not be called from user code.
 */
uint8_t in8(uint16_t port);

} // namespace io
