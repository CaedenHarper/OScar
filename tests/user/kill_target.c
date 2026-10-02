#include <oscar/syscalls.h>
#include <stdint.h>

int main(void) {
    for(;;) {
        oscar_sleep(UINT64_MAX);
    }
}
