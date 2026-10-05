#include <oscar/syscalls.h>
#include <stdint.h>

enum { kUnresponsivePort = 9 };

int main(void) {
    static const uint8_t unresponsive_address[] = {192, 0, 2, 1};
    const int64_t descriptor = oscar_socket(OSCAR_AF_INET, OSCAR_SOCK_STREAM, OSCAR_IPPROTO_TCP);
    if(descriptor < 0) {
        return 1;
    }
    // The test intentionally leaves the descriptor for process cleanup. This keeps
    // the assertion focused on connect request completion rather than close timing.
    const int64_t result = oscar_connect(descriptor, unresponsive_address, kUnresponsivePort);
    if(result >= 0) {
        return 1;
    }
    return 0;
}
