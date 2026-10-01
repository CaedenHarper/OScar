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

static void write_number(uint8_t value) {
    char digits[3];
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

static int parse_address(const char* text, uint8_t address[4]) {
    uint32_t component = 0;
    uint32_t component_index = 0;
    for(uint64_t index = 0;; ++index) {
        const char character = text[index];
        if(character >= '0' && character <= '9') {
            component = component * 10 + (uint32_t)(character - '0');
            if(component > 255) {
                return 0;
            }
            continue;
        }
        if((character == '.' || character == '\0') && component_index < 4) {
            address[component_index++] = (uint8_t)component;
            component = 0;
            if(character == '\0') {
                return component_index == 4;
            }
            continue;
        }
        return 0;
    }
}

void _start(int argc, char** argv) {
    static const char usage[] = "ping: usage: ping <ipv4-address>\n";
    static const char failure[] = "ping: request failed or timed out.\n";
    if(argc != 2 || argv == 0) {
        write_string(usage);
        oscar_exit(1);
    }

    uint8_t address[4];
    if(!parse_address(argv[1], address)) {
        write_string(usage);
        oscar_exit(1);
    }
    write_string("PING ");
    write_string(argv[1]);
    write_string(" ... ");
    if(oscar_ping(address, 200) < 0) {
        write_string(failure);
        oscar_exit(1);
    }
    write_string("reply from ");
    write_number(address[0]);
    write_string(".");
    write_number(address[1]);
    write_string(".");
    write_number(address[2]);
    write_string(".");
    write_number(address[3]);
    write_string("\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
