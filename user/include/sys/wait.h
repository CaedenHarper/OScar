#pragma once

#include <sys/types.h>

enum {
    WNOHANG = 1,
};

/** Wait for a child process and return its process identifier. */
pid_t waitpid(pid_t process_id, int* status, int options);
