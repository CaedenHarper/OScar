#include <oscar/http.h>
#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
    static const char usage[] = "httpget: usage: httpget http://host[:port]/path\n";
    static const char failure[] = "httpget: request failed: ";
    static const char unknown_error[] = "unknown HTTP error";
    static const char status_suffix[] = ", status=";
    static const uint64_t kBodyCapacity = 8192;
    if(argc != 2 || argv == 0) {
        oscar_write_string(usage);
        _Exit(1);
    }
    char body[kBodyCapacity];
    uint64_t length = 0;
    uint16_t status = 0;
    const int64_t result = oscar_http_get(argv[1], body, sizeof(body), &length, &status);
    if(result < 0) {
        oscar_write_string(failure);
        const char* message = unknown_error;
        switch(result) {
            case OSCAR_HTTP_ERROR_INVALID_URL:
                message = "invalid URL (only http:// URLs are supported)";
                break;
            case OSCAR_ERROR_NAME_NOT_FOUND:
                message = "DNS lookup failed";
                break;
            case OSCAR_ERROR_NETWORK_UNAVAILABLE:
                message = "network is unavailable";
                break;
            case OSCAR_ERROR_NETWORK_TIMEOUT:
                message = "network operation timed out";
                break;
            case OSCAR_ERROR_ADDRESS_UNREACHABLE:
                message = "destination address is unreachable";
                break;
            case OSCAR_ERROR_CONNECTION_RESET:
                message = "connection was reset by the peer";
                break;
            case OSCAR_ERROR_NOT_CONNECTED:
                message = "socket is not connected";
                break;
            case OSCAR_HTTP_ERROR_CONNECT:
                message = "could not connect to the server";
                break;
            case OSCAR_HTTP_ERROR_SEND:
                message = "could not send the HTTP request";
                break;
            case OSCAR_HTTP_ERROR_RECEIVE:
                message = "connection failed while receiving the response";
                break;
            case OSCAR_HTTP_ERROR_MALFORMED_RESPONSE:
                message = "server returned a malformed HTTP response";
                break;
            case OSCAR_HTTP_ERROR_RESPONSE_TOO_LARGE:
                message = "response body is larger than the 8192-byte buffer";
                break;
            case OSCAR_HTTP_ERROR_UNSUPPORTED_RESPONSE:
                message = "server returned an unsuccessful HTTP status";
                break;
            default:
                break;
        }
        oscar_write_string(message);
        oscar_write_string(" (error=");
        oscar_write_uint((uint64_t)(-result));
        oscar_write_string(")");
        oscar_write_string(status_suffix);
        oscar_write_uint(status);
        oscar_write_string("\n");
        _Exit(1);
    }
    oscar_write_string("HTTP status: ");
    oscar_write_uint(status);
    oscar_write_string("\n\n");
    (void)write(1, body, length);
    if(length == 0 || body[length - 1] != '\n') {
        oscar_write_string("\n");
    }
    _Exit(0);
}
