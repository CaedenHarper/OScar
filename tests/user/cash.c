#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

enum {
    kLineCapacity = 128,
};

__attribute__((noreturn)) static void exit_shell(void) {
    oscar_exit(0);
}

void _start(void) {
    static const char prompt[] = "cash> ";
    static const char input_prefix[] = "cash: read ";
    static const char input_suffix[] = " bytes.\n";
    static const char read_failure[] = "cash: input read failed.\n";
    static char line[kLineCapacity];

    for(;;) {
        (void)oscar_write(1, prompt, sizeof(prompt) - 1);
        const int64_t count = oscar_read(0, line, sizeof(line));
        if(count < 0) {
            (void)oscar_write(1, read_failure, sizeof(read_failure) - 1);
            exit_shell();
        }
        if(count == 0) {
            continue;
        }

        // Command parsing is intentionally deferred. This confirms that input is
        // delivered as complete edited lines before the shell starts dispatching
        // built-ins or external programs.
        (void)oscar_write(1, input_prefix, sizeof(input_prefix) - 1);
        char digits[4];
        uint32_t digit_count = 0;
        uint64_t value = (uint64_t)count;
        do {
            digits[digit_count++] = (char)('0' + (value % 10));
            value /= 10;
        } while(value != 0 && digit_count < sizeof(digits));
        while(digit_count != 0) {
            --digit_count;
            (void)oscar_write(1, &digits[digit_count], 1);
        }
        (void)oscar_write(1, input_suffix, sizeof(input_suffix) - 1);
    }
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
