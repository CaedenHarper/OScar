#include <stdint.h>

static char output[512];
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
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while(value != 0);

    while(count != 0) {
        append_char(digits[--count]);
    }
}

static uint64_t write_output(const char* buffer, uint64_t length) {
    uint64_t number = 0;
    __asm__ volatile("int $0x80" : "+a"(number) : "D"(buffer), "S"(length) : "rcx", "r11", "memory");
    return number;
}

__attribute__((noreturn)) static void exit_program(void) {
    uint64_t number = 1;
    __asm__ volatile("int $0x80" : "+a"(number) : : "rcx", "r11", "memory");
    for(;;) {
        __asm__ volatile("pause");
    }
}

void _start(void) {
    const char prefix[] = "Primes: ";
    for(uint64_t index = 0; index < sizeof(prefix) - 1; ++index) {
        append_char(prefix[index]);
    }

    int first = 1;
    for(uint32_t value = 2; value <= 97; ++value) {
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
