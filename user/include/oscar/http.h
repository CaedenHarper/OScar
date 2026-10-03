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
    OSCAR_HTTP_ERROR_BODY_CALLBACK = -107,
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

/** Receives response-body fragments while an HTTP response is parsed. */
typedef int (*oscar_http_body_callback)(const void* data, uint64_t length, void* context);

enum {
    OSCAR_HTTP_HEADER_CAPACITY = 4096,
    OSCAR_HTTP_CHUNK_LINE_CAPACITY = 32,
};

/** Incremental HTTP response parser state. Initialize before feeding network data. */
struct oscar_http_response_parser {
    char header[OSCAR_HTTP_HEADER_CAPACITY];
    char chunk_line[OSCAR_HTTP_CHUNK_LINE_CAPACITY];
    uint64_t header_length;
    uint64_t content_length;
    uint64_t body_received;
    uint64_t chunk_remaining;
    uint64_t chunk_line_length;
    uint16_t status_code;
    uint32_t has_content_length;
    uint32_t chunked;
    uint32_t state;
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

/** Initialize an incremental response parser before receiving an HTTP response. */
void oscar_http_response_parser_init(struct oscar_http_response_parser* parser);

/** Feed one received network fragment to the parser and body callback. */
int64_t oscar_http_response_parser_feed(
    struct oscar_http_response_parser* parser,
    const void* data,
    uint64_t length,
    oscar_http_body_callback callback,
    void* context
);

/** Finish parsing a response after the TCP stream closes. */
int64_t oscar_http_response_parser_finish(const struct oscar_http_response_parser* parser);

/** Fetch an HTTP resource and stream body fragments to the caller. */
int64_t oscar_http_get_stream(
    const char* url_text,
    oscar_http_body_callback callback,
    void* context,
    uint16_t* status_code
);

/** Fetch an HTTP resource and copy its response body into caller-owned storage. */
int64_t oscar_http_get(const char* url_text, char* body, uint64_t capacity, uint64_t* length, uint16_t* status_code);
