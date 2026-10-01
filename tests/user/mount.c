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

void _start(void) {
    struct oscar_statfs status;
    if(oscar_statfs(&status) < 0) {
        write_string("mount: unable to read mount table.\n");
        oscar_exit(1);
    }
    write_string(status.device);
    write_string(" on ");
    write_string(status.mount_point);
    write_string(" type ");
    write_string(status.filesystem);
    write_string(" (rw)\n");
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
