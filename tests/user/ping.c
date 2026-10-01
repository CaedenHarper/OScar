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
    static const char timeout[] = "ping: request timed out.\n";
    static const uint64_t kProbeCount = 4;
    static const uint64_t kTimeoutTicks = 200;
    static const uint64_t kProbeIntervalTicks = 100;
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
    write_string("\n");

    uint64_t received = 0;
    uint64_t total_time = 0;
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;
    for(uint64_t sequence = 1; sequence <= kProbeCount; ++sequence) {
        const int64_t elapsed = oscar_ping(address, kTimeoutTicks);
        if(elapsed < 0) {
            write_string(timeout);
        } else {
            const uint64_t elapsed_ms = (uint64_t)elapsed;
            ++received;
            total_time += elapsed_ms;
            minimum = elapsed_ms < minimum ? elapsed_ms : minimum;
            maximum = elapsed_ms > maximum ? elapsed_ms : maximum;
            write_string("64 bytes from ");
            write_string(argv[1]);
            write_string(": icmp_seq=");
            write_number(sequence);
            write_string(" ttl=64 time=");
            write_number(elapsed_ms);
            write_string(" ms\n");
        }
        if(sequence != kProbeCount) {
            oscar_sleep(kProbeIntervalTicks);
        }
    }

    write_string("\n--- ");
    write_string(argv[1]);
    write_string(" ping statistics ---\n");
    write_number(kProbeCount);
    write_string(" packets transmitted, ");
    write_number(received);
    write_string(" received, ");
    write_number(((kProbeCount - received) * 100) / kProbeCount);
    write_string("% packet loss\n");
    if(received != 0) {
        write_string("rtt min/avg/max = ");
        write_number(minimum);
        write_string("/");
        write_number(total_time / received);
        write_string("/");
        write_number(maximum);
        write_string(" ms\n");
    }
    if(received == 0) {
        oscar_exit(1);
    }
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
