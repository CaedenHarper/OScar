#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdint.h>
#include <stdlib.h>

enum {
    kDecimalBase = 10,
    kAbortStatus = 134,
};

__attribute__((noreturn)) void exit(int status) {
    oscar_exit(status);
}

__attribute__((noreturn)) void _Exit(int status) {
    oscar_exit(status);
}

__attribute__((noreturn)) void abort(void) {
    oscar_exit(kAbortStatus);
}

int atoi(const char* text) {
    int64_t value = 0;
    return oscar_parse_i64(text, &value) ? (int)value : 0;
}

long strtol(const char* text, char** end, int base) {
    int64_t value = 0;
    if(base != kDecimalBase || !oscar_parse_i64(text, &value)) {
        if(end != (char**)0) {
            *end = (char*)text;
        }
        return 0;
    }
    if(end != (char**)0) {
        *end = (char*)&text[oscar_strlen(text)];
    }
    return (long)value;
}

unsigned long strtoul(const char* text, char** end, int base) {
    int64_t value = strtol(text, end, base);
    return value < 0 ? 0 : (unsigned long)value;
}
