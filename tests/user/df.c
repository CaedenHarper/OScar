#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

int main(void) {
    struct oscar_statfs status;
    if(oscar_statfs(&status) < 0) {
        oscar_write_string("df: unable to read filesystem status.\n");
        oscar_exit(1);
    }

    const uint64_t used_blocks = status.total_blocks - status.free_blocks;
    const uint64_t usage_percent = status.total_blocks == 0 ? 0 : used_blocks * 100 / status.total_blocks;
    oscar_write_string("Filesystem\tBlocks\tUsed\tFree\tUse%\tMounted on\n");
    oscar_write_string(status.device);
    oscar_write_string("\t");
    oscar_write_uint(status.total_blocks);
    oscar_write_string("\t");
    oscar_write_uint(used_blocks);
    oscar_write_string("\t");
    oscar_write_uint(status.free_blocks);
    oscar_write_string("\t");
    oscar_write_uint(usage_percent);
    oscar_write_string("%\t");
    oscar_write_string(status.mount_point);
    oscar_write_string("\n");
    oscar_exit(0);
}
