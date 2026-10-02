#pragma once

#include <stdint.h>

/** Return the number of characters before the terminating null character. */
uint64_t oscar_strlen(const char* string);

/** Return nonzero when two null-terminated strings contain the same text. */
int oscar_streq(const char* left, const char* right);

/** Parse a signed base-10 integer, returning nonzero on success. */
int oscar_parse_i64(const char* text, int64_t* value);
