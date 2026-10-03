#pragma once

/** Terminate the calling process with status. */
__attribute__((noreturn)) void exit(int status);

/** Terminate the calling process immediately with status. */
// NOLINTNEXTLINE(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp) POSIX requires this name.
__attribute__((noreturn)) void _Exit(int status);

/** Terminate the calling process abnormally. */
__attribute__((noreturn)) void abort(void);

/** Convert a decimal string to int, returning zero when it is invalid. */
int atoi(const char* text);

/** Convert a decimal string to long and optionally return its end position. */
long strtol(const char* text, char** end, int base);

/** Convert a decimal string to unsigned long and optionally return its end position. */
unsigned long strtoul(const char* text, char** end, int base);
