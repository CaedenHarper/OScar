#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kOutputCapacity = 512,
    kDecimalBase = 10,
    kFirstPrimeCandidate = 2,
    kLastPrimeCandidate = 97,
};

// These buffers are process-local state used while constructing the output.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static char output[kOutputCapacity];
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint64_t output_length;

static int is_prime(uint32_t value) {
    if(value < 2) {
        return 0;
    }
    for(uint32_t divisor = 2; divisor <= value / divisor; ++divisor) {
        if(value % divisor == 0) {
            return 0;
        }
    }
    return 1;
}

static void append_char(char value) {
    output[output_length++] = value;
}

static void append_number(uint32_t value) {
    char digits[3];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (value % kDecimalBase));
        value /= kDecimalBase;
    } while(value != 0);

    while(count != 0) {
        append_char(digits[--count]);
    }
}

static uint64_t write_output(const char* buffer, uint64_t length) {
    return (uint64_t)oscar_write(1, buffer, length);
}

__attribute__((noreturn)) static void exit_program(void) {
    oscar_exit(0);
}

int main(void) {
    const char prefix[] = "Primes: ";
    for(uint64_t index = 0; index < sizeof(prefix) - 1; ++index) {
        append_char(prefix[index]);
    }

    int first = 1;
    for(uint32_t value = kFirstPrimeCandidate; value <= kLastPrimeCandidate; ++value) {
        if(!is_prime(value)) {
            continue;
        }
        if(!first) {
            append_char(',');
            append_char(' ');
        }
        append_number(value);
        first = 0;
    }
    append_char('\n');
    (void)write_output(output, output_length);
    exit_program();
}
