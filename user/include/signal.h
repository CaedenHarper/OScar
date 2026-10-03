#pragma once

#include <sys/types.h>

enum {
    SIGTERM = 15,
};

/** Send a termination signal; OScar currently supports SIGTERM only. */
int kill(pid_t process_id, int signal);
