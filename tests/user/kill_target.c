#include <oscar/syscalls.h>
#include <stdint.h>

// The linker requires this exact externally visible entry point for a user ELF.
// NOLINTNEXTLINE(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, misc-use-internal-linkage)
void _start(void) {
    for(;;) {
        oscar_sleep(UINT64_MAX);
    }
}
