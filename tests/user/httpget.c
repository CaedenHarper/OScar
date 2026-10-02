#include <oscar/http.h>
#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

int main(int argc, char** argv) {
    static const char usage[] = "httpget: usage: httpget http://host[:port]/path\n";
    static const char failure[] = "httpget: request failed, error=";
    static const char status_suffix[] = ", status=";
    static const uint64_t kBodyCapacity = 8192;
    if(argc != 2 || argv == 0) {
        oscar_write_string(usage);
        oscar_exit(1);
    }
    char body[kBodyCapacity];
    uint64_t length = 0;
    uint16_t status = 0;
    const int64_t result = oscar_http_get(argv[1], body, sizeof(body), &length, &status);
    if(result < 0) {
        oscar_write_string(failure);
        oscar_write_uint((uint64_t)(-result));
        oscar_write_string(status_suffix);
        oscar_write_uint(status);
        oscar_write_string("\n");
        oscar_exit(1);
    }
    oscar_write_string("HTTP status: ");
    oscar_write_uint(status);
    oscar_write_string("\n\n");
    (void)oscar_write(1, body, length);
    if(length == 0 || body[length - 1] != '\n') {
        oscar_write_string("\n");
    }
    oscar_exit(0);
}
