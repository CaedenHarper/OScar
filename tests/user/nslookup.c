#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

static void write_address(const uint8_t address[4]) {
    for(uint32_t index = 0; index < 4; ++index) {
        if(index != 0) {
            oscar_write_string(".");
        }
        oscar_write_uint(address[index]);
    }
}

int main(int argc, char** argv) {
    static const char usage[] = "nslookup: usage: nslookup <hostname>\n";
    static const char failure[] = "nslookup: lookup failed.\n";
    static const uint64_t kTimeoutTicks = 200;
    if(argc != 2 || argv == 0) {
        oscar_write_string(usage);
        oscar_exit(1);
    }

    uint8_t address[4];
    if(oscar_resolve(argv[1], address, kTimeoutTicks) < 0) {
        oscar_write_string(failure);
        oscar_exit(1);
    }
    oscar_write_string("Name: ");
    oscar_write_string(argv[1]);
    oscar_write_string("\nAddress: ");
    write_address(address);
    oscar_write_string("\n");
    oscar_exit(0);
}
