#ifndef OSCAR_STDIO_H
#define OSCAR_STDIO_H

#include <stdint.h>

/** Write a null-terminated string to standard output. */
void oscar_write_string(const char* string);

/** Write an unsigned decimal integer to standard output. */
void oscar_write_uint(uint64_t value);

#endif
