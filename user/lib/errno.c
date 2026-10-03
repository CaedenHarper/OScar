#include <errno.h>

int errno; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) POSIX exposes mutable process-local errno.
