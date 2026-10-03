#include <oscar/string.h>
#include <stdint.h>
#include <string.h>

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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) required by the C standard signature.
void* memcpy(void* destination, const void* source, size_t count) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    for(size_t index = 0; index < count; ++index) {
        output[index] = input[index];
    }
    return destination;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) required by the C standard signature.
void* memmove(void* destination, const void* source, size_t count) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    if(output <= input) {
        for(size_t index = 0; index < count; ++index) {
            output[index] = input[index];
        }
        return destination;
    }
    for(size_t index = count; index != 0; --index) {
        output[index - 1] = input[index - 1];
    }
    return destination;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) required by the C standard signature.
void* memset(void* destination, int value, size_t count) {
    unsigned char* output = (unsigned char*)destination;
    for(size_t index = 0; index < count; ++index) {
        output[index] = (unsigned char)value;
    }
    return destination;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) required by the C standard signature.
int memcmp(const void* left, const void* right, size_t count) {
    const unsigned char* first = (const unsigned char*)left;
    const unsigned char* second = (const unsigned char*)right;
    for(size_t index = 0; index < count; ++index) {
        if(first[index] != second[index]) {
            return first[index] < second[index] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char* string) {
    size_t length = 0;
    while(string[length] != '\0') {
        ++length;
    }
    return length;
}

int strcmp(const char* left, const char* right) {
    size_t index = 0;
    while(left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return (unsigned char)left[index] - (unsigned char)right[index];
}

int strncmp(const char* left, const char* right, size_t count) {
    for(size_t index = 0; index < count; ++index) {
        if(left[index] != right[index] || left[index] == '\0') {
            return (unsigned char)left[index] - (unsigned char)right[index];
        }
    }
    return 0;
}

char* strcpy(char* destination, const char* source) {
    size_t index = 0;
    while((destination[index] = source[index]) != '\0') {
        ++index;
    }
    return destination;
}

char* strncpy(char* destination, const char* source, size_t count) {
    size_t index = 0;
    while(index < count && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    while(index < count) {
        destination[index++] = '\0';
    }
    return destination;
}

char* strchr(const char* string, int character) {
    while(*string != '\0') {
        if(*string == (char)character) {
            return (char*)string;
        }
        ++string;
    }
    return character == '\0' ? (char*)string : (char*)0;
}

char* strrchr(const char* string, int character) {
    const char* match = (const char*)0;
    while(*string != '\0') {
        if(*string == (char)character) {
            match = string;
        }
        ++string;
    }
    if(character == '\0') {
        return (char*)string;
    }
    return (char*)match;
}
