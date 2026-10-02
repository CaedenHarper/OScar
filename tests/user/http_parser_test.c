#include <oscar/http.h>
#include <oscar/string.h>
#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

enum {
    kExpectedPort = 8080,
    kExpectedStatus = 200,
    kExpectedBodyLength = 5,
};

void _start(void) {
    struct oscar_http_url url;
    uint16_t status = 0;
    uint64_t header_length = 0;
    uint64_t content_length = 0;
    uint32_t has_content_length = 0;
    static const char response[] = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    if(oscar_http_parse_url("https://example.com/path", &url) >= 0 ||
       oscar_http_parse_url("http://example.com:8080/path", &url) < 0 || !oscar_streq(url.host, "example.com") ||
       !oscar_streq(url.path, "/path") || url.port != kExpectedPort ||
       oscar_http_parse_response(
           response, sizeof(response) - 1, &status, &header_length, &content_length, &has_content_length
       ) < 0 ||
       status != kExpectedStatus || !has_content_length || content_length != kExpectedBodyLength ||
       header_length == 0) {
        oscar_exit(1);
    }
    oscar_exit(0);
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
