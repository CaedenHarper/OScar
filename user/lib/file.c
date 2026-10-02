#include <oscar/file.h>
#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kFileBufferCapacity = 256,
};

int oscar_copy_file(const char* source, const char* destination) {
    const int64_t input = oscar_open(source, OSCAR_OPEN_READ);
    if(input < 0) {
        return 0;
    }

    int64_t output = oscar_open(destination, OSCAR_OPEN_WRITE);
    if(output < 0 && oscar_create(destination) == 0) {
        output = oscar_open(destination, OSCAR_OPEN_WRITE);
    }
    if(output < 0) {
        (void)oscar_close(input);
        return 0;
    }

    char buffer[kFileBufferCapacity];
    int success = 1;
    for(;;) {
        const int64_t received = oscar_read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        if(oscar_write(output, buffer, (uint64_t)received) != received) {
            success = 0;
            break;
        }
    }
    (void)oscar_close(input);
    (void)oscar_close(output);
    return success;
}

int oscar_cat_file(const char* path) {
    const int64_t input = oscar_open(path, OSCAR_OPEN_READ);
    if(input < 0) {
        return 0;
    }

    char buffer[kFileBufferCapacity];
    int success = 1;
    for(;;) {
        const int64_t received = oscar_read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        if(oscar_write(1, buffer, (uint64_t)received) != received) {
            success = 0;
            break;
        }
    }
    (void)oscar_close(input);
    return success;
}
