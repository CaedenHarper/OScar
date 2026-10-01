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

void _start(void) {
    struct oscar_statfs status;
    if(oscar_statfs(&status) < 0) {
        write_string("df: unable to read filesystem status.\n");
        oscar_exit(1);
    }

    const uint64_t used_blocks = status.total_blocks - status.free_blocks;
    const uint64_t usage_percent = status.total_blocks == 0 ? 0 : used_blocks * 100 / status.total_blocks;
    write_string("Filesystem\tBlocks\tUsed\tFree\tUse%\tMounted on\n");
    write_string(status.device);
    write_string("\t");
    write_number(status.total_blocks);
    write_string("\t");
    write_number(used_blocks);
    write_string("\t");
    write_number(status.free_blocks);
    write_string("\t");
    write_number(usage_percent);
    write_string("%\t");
    write_string(status.mount_point);
    write_string("\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
