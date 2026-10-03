#pragma once

#include <stddef.h>

/** Copy count bytes from source to destination; regions must not overlap. */
void* memcpy(void* destination, const void* source, size_t count);

/** Copy count bytes from source to destination, including overlapping regions. */
void* memmove(void* destination, const void* source, size_t count);

/** Fill count bytes of destination with the byte value. */
void* memset(void* destination, int value, size_t count);

/** Compare count bytes and return their unsigned-byte ordering. */
int memcmp(const void* left, const void* right, size_t count);

/** Return the length of a null-terminated string. */
size_t strlen(const char* string);

/** Compare two null-terminated strings. */
int strcmp(const char* left, const char* right);

/** Compare at most count characters from two strings. */
int strncmp(const char* left, const char* right, size_t count);

/** Copy a null-terminated string into destination. */
char* strcpy(char* destination, const char* source);

/** Copy at most count characters into destination. */
char* strncpy(char* destination, const char* source, size_t count);

/** Find the first occurrence of character in string. */
char* strchr(const char* string, int character);

/** Find the last occurrence of character in string. */
char* strrchr(const char* string, int character);
