#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdbool.h>
#include <stdint.h>

// The recursive traversal needs shared failure state so one failed child stops
// the final result without changing the public command interface.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool failed;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

// NOLINTNEXTLINE(misc-no-recursion)
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
        enum { kPathCapacity = 512 };
        char child_path[kPathCapacity];
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

int main(void) {
    static const char root[] = "/";
    const uint64_t size = directory_size(root);
    if(failed) {
        oscar_write_string("du: unable to read directory.\n");
        oscar_exit(1);
    }
    oscar_write_uint(size);
    oscar_write_string("\t");
    oscar_write_string(root);
    oscar_write_string("\n");
    oscar_exit(0);
}
