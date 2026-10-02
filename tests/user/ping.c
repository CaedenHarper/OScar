#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

static int parse_address(const char* text, uint8_t address[4]) {
    enum {
        kDecimalBase = 10,
        kMaximumIpv4Octet = 255,
    };
    uint32_t component = 0;
    uint32_t component_index = 0;
    for(uint64_t index = 0;; ++index) {
        const char character = text[index];
        if(character >= '0' && character <= '9') {
            component = (component * kDecimalBase) + (uint32_t)(character - '0');
            if(component > kMaximumIpv4Octet) {
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

int main(int argc, char** argv) {
    static const char usage[] = "ping: usage: ping <ipv4-address-or-hostname>\n";
    static const char timeout[] = "ping: request timed out.\n";
    static const char resolve_failure[] = "ping: could not resolve host.\n";
    static const uint64_t kProbeCount = 4;
    static const uint64_t kTimeoutTicks = 200;
    static const uint64_t kProbeIntervalTicks = 100;
    static const uint64_t kPercentageBase = 100;
    if(argc != 2 || argv == 0) {
        oscar_write_string(usage);
        oscar_exit(1);
    }

    uint8_t address[4];
    if(!parse_address(argv[1], address)) {
        if(oscar_resolve(argv[1], address, kTimeoutTicks) < 0) {
            oscar_write_string(resolve_failure);
            oscar_exit(1);
        }
    }
    oscar_write_string("PING ");
    oscar_write_string(argv[1]);
    oscar_write_string("\n");

    uint64_t received = 0;
    uint64_t total_time = 0;
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;
    for(uint64_t sequence = 1; sequence <= kProbeCount; ++sequence) {
        const int64_t elapsed = oscar_ping(address, kTimeoutTicks);
        if(elapsed < 0) {
            oscar_write_string(timeout);
        } else {
            const uint64_t elapsed_ms = (uint64_t)elapsed;
            ++received;
            total_time += elapsed_ms;
            minimum = elapsed_ms < minimum ? elapsed_ms : minimum;
            maximum = elapsed_ms > maximum ? elapsed_ms : maximum;
            oscar_write_string("64 bytes from ");
            oscar_write_string(argv[1]);
            oscar_write_string(": icmp_seq=");
            oscar_write_uint(sequence);
            oscar_write_string(" ttl=64 time=");
            oscar_write_uint(elapsed_ms);
            oscar_write_string(" ms\n");
        }
        if(sequence != kProbeCount) {
            oscar_sleep(kProbeIntervalTicks);
        }
    }

    oscar_write_string("\n--- ");
    oscar_write_string(argv[1]);
    oscar_write_string(" ping statistics ---\n");
    oscar_write_uint(kProbeCount);
    oscar_write_string(" packets transmitted, ");
    oscar_write_uint(received);
    oscar_write_string(" received, ");
    oscar_write_uint(((kProbeCount - received) * kPercentageBase) / kProbeCount);
    oscar_write_string("% packet loss\n");
    if(received != 0) {
        oscar_write_string("rtt min/avg/max = ");
        oscar_write_uint(minimum);
        oscar_write_string("/");
        oscar_write_uint(total_time / received);
        oscar_write_string("/");
        oscar_write_uint(maximum);
        oscar_write_string(" ms\n");
    }
    if(received == 0) {
        oscar_exit(1);
    }
    oscar_exit(0);
}
