#include <oscar/http.h>
#include <oscar/stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    kBodyCapacity = 32,
    kExpectedPort = 8080,
    kExpectedStatus = 200,
    kExpectedBodyLength = 5,
    kExpectedChunkedBodyLength = 11,
};

struct Body {
    char data[kBodyCapacity];
    uint64_t length;
};

static int append_body(const void* data, uint64_t length, void* context) {
    struct Body* body = (struct Body*)context;
    if(body->length + length > sizeof(body->data)) {
        return -1;
    }
    for(uint64_t index = 0; index < length; ++index) {
        body->data[body->length + index] = ((const char*)data)[index];
    }
    body->length += length;
    return 0;
}

static int test_incremental_content_length(void) {
    static const char fragments[][32] = {
        "HTTP/1.1 200 OK\r\nContent-Len",
        "gth: 5\r\n\r\nhe",
        "llo",
    };
    static const char expected[] = "hello";
    struct Body body = {{0}, 0};
    struct oscar_http_response_parser parser;
    oscar_http_response_parser_init(&parser);
    for(uint64_t index = 0; index < sizeof(fragments) / sizeof(fragments[0]); ++index) {
        if(oscar_http_response_parser_feed(&parser, fragments[index], strlen(fragments[index]), append_body, &body) <
           0) {
            return 0;
        }
    }
    return oscar_http_response_parser_finish(&parser) == 0 && parser.status_code == kExpectedStatus &&
           body.length == kExpectedBodyLength && memcmp(body.data, expected, body.length) == 0;
}

static int test_chunked_response(void) {
    static const char response[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;test=yes\r\n "
                                   "world\r\n0\r\nX-Test: yes\r\n\r\n";
    static const char expected[] = "hello world";
    struct Body body = {{0}, 0};
    struct oscar_http_response_parser parser;
    oscar_http_response_parser_init(&parser);
    for(uint64_t index = 0; index < sizeof(response) - 1; index += 3) {
        const uint64_t remaining = sizeof(response) - 1 - index;
        const uint64_t length = remaining < 3 ? remaining : 3;
        const int64_t feed_result =
            oscar_http_response_parser_feed(&parser, &response[index], length, append_body, &body);
        if(feed_result < 0) {
            oscar_write_string("chunk feed failed at offset ");
            oscar_write_uint(index);
            oscar_write_string("\n");
            return 0;
        }
    }
    const int64_t finish_result = oscar_http_response_parser_finish(&parser);
    if(finish_result < 0) {
        oscar_write_string("chunk finish failed\n");
        return 0;
    }
    return parser.chunked && body.length == kExpectedChunkedBodyLength && memcmp(body.data, expected, body.length) == 0;
}

static int test_rejects_incomplete_responses(void) {
    static const char incomplete[] = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabc";
    static const char malformed_chunk[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n";
    struct Body body = {{0}, 0};
    struct oscar_http_response_parser parser;
    oscar_http_response_parser_init(&parser);
    if(oscar_http_response_parser_feed(&parser, incomplete, sizeof(incomplete) - 1, append_body, &body) < 0 ||
       oscar_http_response_parser_finish(&parser) >= 0) {
        return 0;
    }
    oscar_http_response_parser_init(&parser);
    return oscar_http_response_parser_feed(&parser, malformed_chunk, sizeof(malformed_chunk) - 1, append_body, &body) <
           0;
}

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
        oscar_write_string("HTTP parser metadata test failed.\n");
        _Exit(1);
    }
    if(!test_incremental_content_length()) {
        oscar_write_string("HTTP parser content-length test failed.\n");
        _Exit(1);
    }
    if(!test_chunked_response()) {
        oscar_write_string("HTTP parser chunked test failed.\n");
        _Exit(1);
    }
    if(!test_rejects_incomplete_responses()) {
        oscar_write_string("HTTP parser malformed-response test failed.\n");
        _Exit(1);
    }
    _Exit(0);
}
