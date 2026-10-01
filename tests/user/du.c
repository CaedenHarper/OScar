#include <oscar/syscalls.h>
#include <stdbool.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-pro-type-member-init)

static bool failed;

static uint64_t string_length(const char* string) {
    uint64_t length = 0;
    while(string[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_string(const char* string) {
    (void)oscar_write(1, string, string_length(string));
}

static void write_number(uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    if(value == 0) {
        write_string("0");
        return;
    }
    while(value != 0) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }
    while(length != 0) {
        --length;
        (void)oscar_write(1, &digits[length], 1);
    }
}

static bool join_path(const char* parent, const char* name, char* output, uint64_t capacity) {
    uint64_t position = 0;
    while(parent[position] != '\0') {
        if(position + 1 >= capacity) {
            return false;
        }
        output[position] = parent[position];
        ++position;
    }
    if(position != 1 || output[0] != '/') {
        if(position + 1 >= capacity) {
            return false;
        }
        output[position++] = '/';
    }
    for(uint64_t index = 0; name[index] != '\0'; ++index) {
        if(position + 1 >= capacity) {
            return false;
        }
        output[position++] = name[index];
    }
    output[position] = '\0';
    return true;
}

static uint64_t directory_size(const char* path) {
    uint64_t total = 0;
    for(uint64_t index = 0;; ++index) {
        struct oscar_dirent entry;
        const int64_t result = oscar_readdir(path, index, &entry);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            break;
        }
        if(result < 0) {
            failed = true;
            return 0;
        }
        char child_path[512];
        if(!join_path(path, entry.name, child_path, sizeof(child_path))) {
            failed = true;
            return 0;
        }
        if(entry.type == OSCAR_NODE_DIRECTORY) {
            total += directory_size(child_path);
        } else {
            total += entry.size;
        }
    }
    return total;
}

void _start(void) {
    static const char root[] = "/";
    const uint64_t size = directory_size(root);
    if(failed) {
        write_string("du: unable to read directory.\n");
        oscar_exit(1);
    }
    write_number(size);
    write_string("\t");
    write_string(root);
    write_string("\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-pro-type-member-init)
