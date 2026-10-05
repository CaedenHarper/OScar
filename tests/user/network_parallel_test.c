#include <oscar/syscalls.h>
#include <stdint.h>

enum { kUnresponsivePort = 9 };
enum { kConnectionCycles = 3 };

static int run_connection_cycle(void) {
    static const uint8_t unresponsive_address[] = {192, 0, 2, 1};
    const char data[] = "not connected";
    const int64_t descriptor = oscar_socket(OSCAR_AF_INET, OSCAR_SOCK_STREAM, OSCAR_IPPROTO_TCP);
    if(descriptor < 0) {
        return 0;
    }
    const int send_succeeded = oscar_send(descriptor, data, sizeof(data) - 1) >= 0;
    const int connect_succeeded = oscar_connect(descriptor, unresponsive_address, kUnresponsivePort) >= 0;
    const int close_failed = oscar_close(descriptor) < 0;
    if(send_succeeded || connect_succeeded || close_failed) {
        (void)oscar_close(descriptor);
        return 0;
    }
    return 1;
}

int main(void) {
    for(int cycle = 0; cycle < kConnectionCycles; ++cycle) {
        if(!run_connection_cycle()) {
            return 1;
        }
    }
    return 0;
}
