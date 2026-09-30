#include <oscar/syscalls.h>
#include <stdbool.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

static uint64_t string_length(const char* string) {
    uint64_t length = 0;
    while(string[length] != '\0') {
        ++length;
    }
    return length;
}

static bool string_equal(const char* left, const char* right) {
    uint64_t index = 0;
    while(left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static void write_text(const char* text) {
    (void)oscar_write(1, text, string_length(text));
}

static void _exit_failure(void) {
    write_text("argv/descriptor inheritance test failed.\n");
    oscar_exit(1);
}

void _start(int argc, char** argv) {
    static const char kExpectedFile[] = "Hello from the OScar filesystem.\n";
    char buffer[sizeof(kExpectedFile)];
    if(argc != 3 || argv == 0 || !string_equal(argv[0], "argv_test") || !string_equal(argv[1], "alpha") ||
       !string_equal(argv[2], "beta")) {
        _exit_failure();
    }

    const int64_t received = oscar_read(3, buffer, sizeof(buffer) - 1);
    if(received != (int64_t)(sizeof(kExpectedFile) - 1)) {
        _exit_failure();
    }
    buffer[received] = '\0';
    if(!string_equal(buffer, kExpectedFile)) {
        _exit_failure();
    }
    write_text("argv and descriptor inheritance test passed.\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
