#pragma once

extern int
    errno; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) POSIX exposes mutable process-local errno.

enum {
    EINVAL = 22,
    EIO = 5,
    EACCES = 13,
    ENOENT = 2,
    ENOMEM = 12,
    ENOSPC = 28,
    ENOTDIR = 20,
    ENOTCONN = 107,
    EPERM = 1,
};
