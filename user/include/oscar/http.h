#pragma once

#include <stdint.h>

enum {
    OSCAR_HTTP_ERROR_INVALID_URL = -100,
    OSCAR_HTTP_ERROR_RESPONSE_TOO_LARGE = -101,
    OSCAR_HTTP_ERROR_MALFORMED_RESPONSE = -102,
    OSCAR_HTTP_ERROR_UNSUPPORTED_RESPONSE = -103,
    OSCAR_HTTP_ERROR_CONNECT = -104,
    OSCAR_HTTP_ERROR_SEND = -105,
    OSCAR_HTTP_ERROR_RECEIVE = -106,
};

enum {
    OSCAR_HTTP_HOST_CAPACITY = 128,
    OSCAR_HTTP_PATH_CAPACITY = 256,
};

struct oscar_http_url {
    char host[OSCAR_HTTP_HOST_CAPACITY];
    char path[OSCAR_HTTP_PATH_CAPACITY];
    uint16_t port;
};

/** Parse an http:// URL into host, port, and path components. */
int64_t oscar_http_parse_url(const char* text, struct oscar_http_url* url);

/** Parse an HTTP response header block and return its status and body metadata. */
int64_t oscar_http_parse_response(
    const char* response,
    uint64_t length,
    uint16_t* status_code,
    uint64_t* header_length,
    uint64_t* content_length,
    uint32_t* has_content_length
);

/** Fetch an HTTP resource and copy its response body into caller-owned storage. */
int64_t oscar_http_get(const char* url_text, char* body, uint64_t capacity, uint64_t* length, uint16_t* status_code);
