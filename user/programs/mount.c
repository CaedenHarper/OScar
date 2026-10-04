#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    struct oscar_statfs status;
    if(oscar_statfs(&status) < 0) {
        oscar_write_string("mount: unable to read mount table.\n");
        _Exit(1);
    }
    oscar_write_string(status.device);
    oscar_write_string(" on ");
    oscar_write_string(status.mount_point);
    oscar_write_string(" type ");
    oscar_write_string(status.filesystem);
    oscar_write_string(" (rw)\n");
    _Exit(0);
}
