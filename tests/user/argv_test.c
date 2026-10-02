#include <oscar/stdio.h>
#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdbool.h>
#include <stdint.h>

static void exit_failure(void) {
    oscar_write_string("argv/descriptor inheritance test failed.\n");
    oscar_exit(1);
}

// The linker requires this exact externally visible entry point for a user ELF.
// NOLINTNEXTLINE(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
void _start(int argc, char** argv) {
    static const char kExpectedFile[] = "Hello from the OScar filesystem.\n";
    char buffer[sizeof(kExpectedFile)];
    if(argc != 3 || argv == 0 || !oscar_streq(argv[0], "argv_test") || !oscar_streq(argv[1], "alpha") ||
       !oscar_streq(argv[2], "beta")) {
        exit_failure();
    }

    const int64_t received = oscar_read(3, buffer, sizeof(buffer) - 1);
    if(received != (int64_t)(sizeof(kExpectedFile) - 1)) {
        exit_failure();
    }
    buffer[received] = '\0';
    if(!oscar_streq(buffer, kExpectedFile)) {
        exit_failure();
    }
    oscar_write_string("argv and descriptor inheritance test passed.\n");
    oscar_exit(0);
}
