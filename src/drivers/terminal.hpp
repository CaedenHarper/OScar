#pragma once

#include <stdint.h>

namespace terminal {

/** Initialize the single kernel console backed by the generic keyboard and serial drivers. */
bool initialize();

/** Return whether the kernel console has been initialized. */
bool is_available();

/** Read one completed, line-buffered console input record into caller-owned storage. */
int64_t read(char* buffer, uint64_t length);

/** Write bytes to the console output backend. The caller owns the source storage. */
int64_t write(const char* buffer, uint64_t length);

} // namespace terminal
