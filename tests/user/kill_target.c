#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

// NOLINTNEXTLINE(misc-use-internal-linkage) ELF entry point required by linker
void _start(void) {
    for(;;) {
        oscar_sleep(UINT64_MAX);
    }
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
