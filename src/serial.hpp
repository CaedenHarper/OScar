#pragma once

#include <stdint.h>

namespace serial {

void initialize();
void putc(char character);
void write(const char* text);
void write_u64(uint64_t value);
void write_hex(uint64_t value);

} // namespace serial
