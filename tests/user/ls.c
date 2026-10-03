#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    static const char failure[] = "ls: could not read directory.\n";
    struct oscar_dirent entry;
    uint64_t index = 0;

    for(;; ++index) {
        const int64_t result = oscar_readdir(".", index, &entry);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            _Exit(0);
        }
        if(result < 0) {
            oscar_write_string(failure);
            _Exit(1);
        }
        (void)write(1, entry.name, entry.name_length);
        oscar_write_string("\n");
    }
    _Exit(0);
}
