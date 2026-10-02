#include <oscar/http.h>
#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay)

static uint64_t string_length(const char* text) {
    uint64_t length = 0;
    while(text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char* text) {
    (void)oscar_write(1, text, string_length(text));
}

static void write_number(uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    if(value == 0) {
        write_text("0");
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

void _start(int argc, char** argv) {
    static const char usage[] = "httpget: usage: httpget http://host[:port]/path\n";
    static const char failure[] = "httpget: request failed, error=";
    static const char status_suffix[] = ", status=";
    static const uint64_t kBodyCapacity = 8192;
    if(argc != 2 || argv == 0) {
        write_text(usage);
        oscar_exit(1);
    }
    char body[kBodyCapacity];
    uint64_t length = 0;
    uint16_t status = 0;
    const int64_t result = oscar_http_get(argv[1], body, sizeof(body), &length, &status);
    if(result < 0) {
        write_text(failure);
        write_number((uint64_t)(-result));
        write_text(status_suffix);
        write_number(status);
        write_text("\n");
        oscar_exit(1);
    }
    write_text("HTTP status: ");
    write_number(status);
    write_text("\n\n");
    (void)oscar_write(1, body, length);
    if(length == 0 || body[length - 1] != '\n') {
        write_text("\n");
    }
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay)
