#pragma once

#include <sys/types.h>

enum {
    S_IFMT = 0170000,
    S_IFREG = 0100000,
    S_IFDIR = 0040000,
};

struct stat {
    off_t st_size;
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
};

/** Read metadata for a filesystem path. */
int stat(const char* path, struct stat* status);
