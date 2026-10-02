#include <oscar/string.h>
#include <stdint.h>

uint64_t oscar_strlen(const char* string) {
    uint64_t length = 0;
    while(string[length] != '\0') {
        ++length;
    }
    return length;
}

int oscar_streq(const char* left, const char* right) {
    uint64_t index = 0;
    while(left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}
