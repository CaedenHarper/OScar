#include <oscar/syscalls.h>
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

static void write_address(const uint8_t address[4]) {
    for(uint32_t index = 0; index < 4; ++index) {
        if(index != 0) {
            write_string(".");
        }
        write_number(address[index]);
    }
}

void _start(int argc, char** argv) {
    static const char usage[] = "nslookup: usage: nslookup <hostname>\n";
    static const char failure[] = "nslookup: lookup failed.\n";
    static const uint64_t kTimeoutTicks = 200;
    if(argc != 2 || argv == 0) {
        write_string(usage);
        oscar_exit(1);
    }

    uint8_t address[4];
    if(oscar_resolve(argv[1], address, kTimeoutTicks) < 0) {
        write_string(failure);
        oscar_exit(1);
    }
    write_string("Name: ");
    write_string(argv[1]);
    write_string("\nAddress: ");
    write_address(address);
    write_string("\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
