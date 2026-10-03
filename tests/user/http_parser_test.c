#include <oscar/http.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    kExpectedPort = 8080,
    kExpectedStatus = 200,
    kExpectedBodyLength = 5,
};

int main(void) {
    struct oscar_http_url url;
    uint16_t status = 0;
    uint64_t header_length = 0;
    uint64_t content_length = 0;
    uint32_t has_content_length = 0;
    static const char response[] = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    if(oscar_http_parse_url("https://example.com/path", &url) >= 0 ||
       oscar_http_parse_url("http://example.com:8080/path", &url) < 0 || strcmp(url.host, "example.com") != 0 ||
       strcmp(url.path, "/path") != 0 || url.port != kExpectedPort ||
       oscar_http_parse_response(
           response, sizeof(response) - 1, &status, &header_length, &content_length, &has_content_length
       ) < 0 ||
       status != kExpectedStatus || !has_content_length || content_length != kExpectedBodyLength ||
       header_length == 0) {
        _Exit(1);
    }
    _Exit(0);
}
