#pragma once

#include <stdint.h>

namespace serial {

/*
 * Initialize COM1 for polled serial output at 38400 baud, 8N1.
 * Must be called before any other function in this namespace.
 */
void initialize();

/*
 * Write one character to COM1. Newlines are emitted as CR-LF.
 */
void putc(char character);

/*
 * Write a null-terminated string to COM1.
 */
void write(const char* text);

/*
 * Write an unsigned 64-bit integer in base 10.
 */
void write_u64(uint64_t value);

/*
 * Write an unsigned 64-bit integer as a fixed-width hexadecimal value.
 */
void write_hex(uint64_t value);

} // namespace serial
