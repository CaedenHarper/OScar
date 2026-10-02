#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

int main(void) {
    static const char failure[] = "ls: could not read directory.\n";
    struct oscar_dirent entry;
    uint64_t index = 0;

    for(;; ++index) {
        const int64_t result = oscar_readdir(".", index, &entry);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            oscar_exit(0);
        }
        if(result < 0) {
            oscar_write_string(failure);
            oscar_exit(1);
        }
        (void)oscar_write(1, entry.name, entry.name_length);
        oscar_write_string("\n");
    }
    oscar_exit(0);
}
