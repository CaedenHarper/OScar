#include <fcntl.h>
#include <oscar/stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    kExpectedNumber = 42,
    kDecimalBase = 10,
    kDirectoryMode = 0755,
    kFileMode = 0644,
};

static int fail(const char* message) {
    oscar_write_string(message);
    return 1;
}

int main(void) {
    static const char path[] = "/libc-test-file";
    static const char directory[] = "/libc-test-directory";
    static const char contents[] = "libc wrappers work\n";
    char buffer[sizeof(contents)];
    char copied[sizeof(contents)];
    struct stat status;
    int descriptors[2];

    if(strcmp("alpha", "alpha") != 0 || strcmp("alpha", "beta") >= 0 || strlen(contents) != sizeof(contents) - 1) {
        return fail("libc: string test failed.\n");
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling) bounded test input.
    memcpy(copied, contents, sizeof(contents));
    if(memcmp(copied, contents, sizeof(contents)) != 0) {
        return fail("libc: memory test failed.\n");
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling) testing overlap handling.
    memmove(&copied[1], copied, sizeof(contents) - 1);
    if(copied[1] != 'l') {
        return fail("libc: overlap test failed.\n");
    }
    char* conversion_end = (char*)0;
    if(strtol("42", &conversion_end, kDecimalBase) != kExpectedNumber || *conversion_end != '\0') {
        return fail("libc: conversion test failed.\n");
    }

    (void)unlink(path);
    (void)rmdir(directory);
    if(mkdir(directory, kDirectoryMode) != 0 || stat(directory, &status) != 0 ||
       (status.st_mode & (uint32_t)S_IFMT) != (uint32_t)S_IFDIR) {
        return fail("libc: directory test failed.\n");
    }
    const int descriptor = open(path, (int)((unsigned)O_CREAT | (unsigned)O_RDWR), kFileMode);
    if(descriptor < 0 || write(descriptor, contents, sizeof(contents) - 1) != (ssize_t)(sizeof(contents) - 1) ||
       lseek(descriptor, 0, 0) != 0 ||
       read(descriptor, buffer, sizeof(contents) - 1) != (ssize_t)(sizeof(contents) - 1) ||
       memcmp(buffer, contents, sizeof(contents) - 1) != 0 || close(descriptor) != 0 || stat(path, &status) != 0 ||
       status.st_size != (off_t)(sizeof(contents) - 1)) {
        return fail("libc: file test failed.\n");
    }
    // NOLINTNEXTLINE(android-cloexec-pipe) OScar does not expose pipe2 yet.
    if(pipe(descriptors) != 0 ||
       write(descriptors[1], contents, sizeof(contents) - 1) != (ssize_t)(sizeof(contents) - 1) ||
       read(descriptors[0], buffer, sizeof(contents) - 1) != (ssize_t)(sizeof(contents) - 1) ||
       close(descriptors[0]) != 0 || close(descriptors[1]) != 0 || unlink(path) != 0 || rmdir(directory) != 0 ||
       getpid() <= 0) {
        return fail("libc: descriptor test failed.\n");
    }
    oscar_write_string("C library smoke test passed.\n");
    return 0;
}
