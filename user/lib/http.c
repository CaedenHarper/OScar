#include <oscar/http.h>
#include <oscar/syscalls.h>
#include <stdint.h>
#include <unistd.h>

// HTTP wire parsing uses protocol offsets and deliberately branches through
// several bounded receive states. These checks are intentionally limited to
// this protocol implementation; unrelated diagnostics must remain visible.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-function-cognitive-complexity,
//             readability-magic-numbers, readability-math-missing-parentheses)

// The parser uses protocol offsets and status-code ranges whose names would obscure the wire format.
enum {
    kHttpDefaultPort = 80,
    kHttpTimeoutTicks = 200,
    kHttpRequestCapacity = 512,
    kHttpResponseCapacity = 8192,
    kHttpReceiveChunk = 4096,
};

static int character_equal_insensitive(char left, char right) {
    if(left >= 'A' && left <= 'Z') {
        left = (char)(left + ('a' - 'A'));
    }
    if(right >= 'A' && right <= 'Z') {
        right = (char)(right + ('a' - 'A'));
    }
    return left == right;
}

static int text_equal_insensitive(const char* left, const char* right, uint64_t length) {
    for(uint64_t index = 0; index < length; ++index) {
        if(!character_equal_insensitive(left[index], right[index])) {
            return 0;
        }
    }
    return 1;
}

static int append_character(char* output, uint64_t capacity, uint64_t* length, char value) {
    if(*length + 1 >= capacity) {
        return 0;
    }
    output[(*length)++] = value;
    output[*length] = '\0';
    return 1;
}

static int parse_decimal(const char* text, uint64_t length, uint64_t* value) {
    if(length == 0) {
        return 0;
    }
    uint64_t result = 0;
    for(uint64_t index = 0; index < length; ++index) {
        if(text[index] < '0' || text[index] > '9' || result > UINT64_MAX / 10) {
            return 0;
        }
        result = result * 10 + (uint64_t)(text[index] - '0');
    }
    *value = result;
    return 1;
}

int64_t oscar_http_parse_url(const char* text, struct oscar_http_url* url) {
    if(text == 0 || url == 0 || text[0] != 'h' || text[1] != 't' || text[2] != 't' || text[3] != 'p' ||
       text[4] != ':' || text[5] != '/' || text[6] != '/') {
        return OSCAR_HTTP_ERROR_INVALID_URL;
    }
    uint64_t index = 7;
    uint64_t host_length = 0;
    while(text[index] != '\0' && text[index] != '/' && text[index] != ':') {
        if(host_length + 1 >= OSCAR_HTTP_HOST_CAPACITY) {
            return OSCAR_HTTP_ERROR_INVALID_URL;
        }
        url->host[host_length++] = text[index++];
    }
    if(host_length == 0) {
        return OSCAR_HTTP_ERROR_INVALID_URL;
    }
    url->host[host_length] = '\0';
    url->port = kHttpDefaultPort;
    if(text[index] == ':') {
        const uint64_t port_start = ++index;
        while(text[index] >= '0' && text[index] <= '9') {
            ++index;
        }
        uint64_t port = 0;
        if(!parse_decimal(&text[port_start], index - port_start, &port) || port == 0 || port > UINT16_MAX) {
            return OSCAR_HTTP_ERROR_INVALID_URL;
        }
        url->port = (uint16_t)port;
    }
    uint64_t path_length = 0;
    if(text[index] == '\0') {
        url->path[0] = '/';
        url->path[1] = '\0';
        return 0;
    }
    if(text[index] != '/') {
        return OSCAR_HTTP_ERROR_INVALID_URL;
    }
    while(text[index] != '\0') {
        if(!append_character(url->path, OSCAR_HTTP_PATH_CAPACITY, &path_length, text[index++])) {
            return OSCAR_HTTP_ERROR_INVALID_URL;
        }
    }
    return 0;
}

static int find_header_end(const char* response, uint64_t length, uint64_t* header_length) {
    for(uint64_t index = 3; index < length; ++index) {
        if(response[index - 3] == '\r' && response[index - 2] == '\n' && response[index - 1] == '\r' &&
           response[index] == '\n') {
            *header_length = index + 1;
            return 1;
        }
    }
    return 0;
}

int64_t oscar_http_parse_response(
    const char* response,
    uint64_t length,
    uint16_t* status_code,
    uint64_t* header_length,
    uint64_t* content_length,
    uint32_t* has_content_length
) {
    if(response == 0 || status_code == 0 || header_length == 0 || content_length == 0 || has_content_length == 0 ||
       length == 0 || !find_header_end(response, length, header_length)) {
        return OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
    }
    if(length < 12 || response[0] != 'H' || response[1] != 'T' || response[2] != 'T' || response[3] != 'P' ||
       response[4] != '/' || response[5] != '1' || response[6] != '.' || (response[7] != '0' && response[7] != '1') ||
       response[8] != ' ' || response[9] < '1' || response[9] > '9' || response[10] < '0' || response[10] > '9' ||
       response[11] < '0' || response[11] > '9') {
        return OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
    }
    *status_code = (uint16_t)((response[9] - '0') * 100 + (response[10] - '0') * 10 + response[11] - '0');
    *content_length = 0;
    *has_content_length = 0;
    uint64_t line_start = 0;
    while(line_start + 1 < *header_length) {
        uint64_t line_end = line_start;
        while(line_end + 1 < *header_length && !(response[line_end] == '\r' && response[line_end + 1] == '\n')) {
            ++line_end;
        }
        if(line_end + 1 >= *header_length) {
            break;
        }
        const uint64_t colon = line_start;
        while(line_start < line_end && response[line_start] != ':') {
            ++line_start;
        }
        if(line_start < line_end && line_start - colon == 14 &&
           text_equal_insensitive(&response[colon], "content-length", 14)) {
            uint64_t value_start = line_start + 1;
            while(value_start < line_end && (response[value_start] == ' ' || response[value_start] == '\t')) {
                ++value_start;
            }
            if(!parse_decimal(&response[value_start], line_end - value_start, content_length)) {
                return OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
            }
            *has_content_length = 1;
        }
        line_start = line_end + 2;
    }
    return 0;
}

static int parse_ipv4(const char* text, uint8_t address[4]) {
    uint32_t component = 0;
    uint32_t count = 0;
    for(uint64_t index = 0;; ++index) {
        const char character = text[index];
        if(character >= '0' && character <= '9') {
            component = component * 10 + (uint32_t)(character - '0');
            if(component > 255) {
                return 0;
            }
        } else if((character == '.' || character == '\0') && count < 4) {
            address[count++] = (uint8_t)component;
            component = 0;
            if(character == '\0') {
                return count == 4;
            }
        } else {
            return 0;
        }
    }
}

int64_t oscar_http_get(const char* url_text, char* body, uint64_t capacity, uint64_t* length, uint16_t* status_code) {
    if(url_text == 0 || body == 0 || length == 0 || status_code == 0 || capacity == 0) {
        return OSCAR_HTTP_ERROR_INVALID_URL;
    }
    struct oscar_http_url url;
    if(oscar_http_parse_url(url_text, &url) < 0) {
        return OSCAR_HTTP_ERROR_INVALID_URL;
    }
    uint8_t address[4];
    if(!parse_ipv4(url.host, address) && oscar_resolve(url.host, address, kHttpTimeoutTicks) < 0) {
        return OSCAR_ERROR_NAME_NOT_FOUND;
    }
    const int64_t socket = oscar_socket(OSCAR_AF_INET, OSCAR_SOCK_STREAM, OSCAR_IPPROTO_TCP);
    if(socket < 0) {
        return socket;
    }
    int64_t result = 0;
    const int64_t connect_result = oscar_connect(socket, address, url.port);
    if(connect_result < 0) {
        result = OSCAR_HTTP_ERROR_CONNECT;
    } else {
        char request[kHttpRequestCapacity];
        uint64_t request_length = 0;
        static const char prefix[] = "GET ";
        static const char middle[] = " HTTP/1.0\r\nHost: ";
        static const char suffix[] = "\r\nConnection: close\r\n\r\n";
        const char* parts[] = {prefix, url.path, middle, url.host, suffix};
        for(uint64_t part = 0; part < sizeof(parts) / sizeof(parts[0]) && result == 0; ++part) {
            for(uint64_t index = 0; parts[part][index] != '\0'; ++index) {
                if(!append_character(request, sizeof(request), &request_length, parts[part][index])) {
                    result = OSCAR_HTTP_ERROR_INVALID_URL;
                    break;
                }
            }
        }
        if(result == 0 && oscar_send(socket, request, request_length) < 0) {
            result = OSCAR_HTTP_ERROR_SEND;
        }
        char raw[kHttpResponseCapacity];
        uint64_t raw_length = 0;
        uint64_t header_length = 0;
        uint64_t content_length = 0;
        uint32_t has_content_length = 0;
        while(result == 0 && header_length == 0) {
            if(raw_length == sizeof(raw)) {
                result = OSCAR_HTTP_ERROR_RESPONSE_TOO_LARGE;
                break;
            }
            const uint64_t available = sizeof(raw) - raw_length;
            const uint64_t receive_length = available < kHttpReceiveChunk ? available : kHttpReceiveChunk;
            const int64_t received = oscar_recv(socket, &raw[raw_length], receive_length);
            if(received <= 0) {
                result = received < 0 ? OSCAR_HTTP_ERROR_RECEIVE : OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
                break;
            }
            raw_length += (uint64_t)received;
            if(find_header_end(raw, raw_length, &header_length)) {
                if(oscar_http_parse_response(
                       raw, raw_length, status_code, &header_length, &content_length, &has_content_length
                   ) < 0) {
                    result = OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
                }
            }
        }
        while(result == 0 &&
              ((has_content_length && raw_length < header_length + content_length) || !has_content_length)) {
            if(raw_length == sizeof(raw)) {
                result = OSCAR_HTTP_ERROR_RESPONSE_TOO_LARGE;
                break;
            }
            const uint64_t available = sizeof(raw) - raw_length;
            const uint64_t receive_length = available < kHttpReceiveChunk ? available : kHttpReceiveChunk;
            const int64_t received = oscar_recv(socket, &raw[raw_length], receive_length);
            if(received < 0) {
                result = received;
                break;
            }
            if(received == 0) {
                break;
            }
            raw_length += (uint64_t)received;
        }
        if(result == 0) {
            const uint64_t available = raw_length > header_length ? raw_length - header_length : 0;
            const uint64_t expected = has_content_length ? content_length : available;
            if(expected > capacity || expected > available) {
                result =
                    expected > capacity ? OSCAR_HTTP_ERROR_RESPONSE_TOO_LARGE : OSCAR_HTTP_ERROR_MALFORMED_RESPONSE;
            } else {
                for(uint64_t index = 0; index < expected; ++index) {
                    body[index] = raw[header_length + index];
                }
                *length = expected;
                if(*status_code < 200 || *status_code >= 300) {
                    result = OSCAR_HTTP_ERROR_UNSUPPORTED_RESPONSE;
                }
            }
        }
    }
    (void)close((int)socket);
    return result;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-function-cognitive-complexity,
//           readability-magic-numbers, readability-math-missing-parentheses)
