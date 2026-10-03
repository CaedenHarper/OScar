#pragma once

enum {
    O_RDONLY = 0,
    O_WRONLY = 1,
    O_RDWR = 2,
    O_CREAT = 0100,
};

/** Open a file, optionally creating it when O_CREAT is present. */
int open(const char* path, int flags, ...);
