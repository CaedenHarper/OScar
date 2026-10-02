#include <oscar/string.h>
#include <stdint.h>

enum {
    kDecimalBase = 10,
};

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

int oscar_parse_i64(const char* text, int64_t* value) {
    uint64_t index = 0;
    uint64_t magnitude = 0;
    int negative = 0;
    if(text[0] == '-') {
        negative = 1;
        index = 1;
    }
    if(text[index] == '\0') {
        return 0;
    }
    while(text[index] != '\0') {
        if(text[index] < '0' || text[index] > '9') {
            return 0;
        }
        magnitude = (magnitude * kDecimalBase) + (uint64_t)(text[index] - '0');
        ++index;
    }
    *value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return 1;
}
