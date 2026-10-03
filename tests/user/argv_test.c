#include <oscar/stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void exit_failure(void) {
    oscar_write_string("argv/descriptor inheritance test failed.\n");
    _Exit(1);
}

int main(int argc, char** argv) {
    static const char kExpectedFile[] = "Hello from the OScar filesystem.\n";
    char buffer[sizeof(kExpectedFile)];
    if(argc != 3 || argv == 0 || strcmp(argv[0], "argv_test") != 0 || strcmp(argv[1], "alpha") != 0 ||
       strcmp(argv[2], "beta") != 0) {
        exit_failure();
    }

    const int64_t received = read(3, buffer, sizeof(buffer) - 1);
    if(received != (int64_t)(sizeof(kExpectedFile) - 1)) {
        exit_failure();
    }
    buffer[received] = '\0';
    if(strcmp(buffer, kExpectedFile) != 0) {
        exit_failure();
    }
    oscar_write_string("argv and descriptor inheritance test passed.\n");
    _Exit(0);
}
