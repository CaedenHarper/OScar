#include <fcntl.h>
#include <oscar/file.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    kFileBufferCapacity = 256,
    kFileMode = 0644,
    kWriteCreateFlags = O_WRONLY + O_CREAT,
};

int copy_file(const char* source, const char* destination) {
    const int input = open(source, O_RDONLY);
    if(input < 0) {
        return 0;
    }

    int output = open(destination, O_WRONLY);
    if(output < 0) {
        output = open(destination, kWriteCreateFlags, kFileMode);
    }
    if(output < 0) {
        (void)close(input);
        return 0;
    }

    char buffer[kFileBufferCapacity];
    int success = 1;
    for(;;) {
        const ssize_t received = read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        if(write(output, buffer, (size_t)received) != received) {
            success = 0;
            break;
        }
    }
    (void)close(input);
    (void)close(output);
    return success;
}

int cat_file(const char* path) {
    const int input = open(path, O_RDONLY);
    if(input < 0) {
        return 0;
    }

    char buffer[kFileBufferCapacity];
    int success = 1;
    for(;;) {
        const ssize_t received = read(input, buffer, sizeof(buffer));
        if(received < 0) {
            success = 0;
            break;
        }
        if(received == 0) {
            break;
        }
        if(write(1, buffer, (size_t)received) != received) {
            success = 0;
            break;
        }
    }
    (void)close(input);
    return success;
}
