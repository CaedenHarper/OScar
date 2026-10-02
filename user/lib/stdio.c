#include <oscar/stdio.h>
#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kDecimalBase = 10,
    kDigitCapacity = 20,
};

void oscar_write_string(const char* string) {
    (void)oscar_write(1, string, oscar_strlen(string));
}

void oscar_write_uint(uint64_t value) {
    char digits[kDigitCapacity];
    uint32_t length = 0;
    if(value == 0) {
        oscar_write_string("0");
        return;
    }
    while(value != 0) {
        digits[length++] = (char)('0' + (value % kDecimalBase));
        value /= kDecimalBase;
    }
    while(length != 0) {
        --length;
        (void)oscar_write(1, &digits[length], 1);
    }
}
