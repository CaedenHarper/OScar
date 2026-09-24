#pragma once

namespace panic {

/*
 * Print a fatal error message and halt the current CPU permanently.
 * This function does not return and requires serial::initialize() first.
 */
[[noreturn]] void halt(const char* message);

} // namespace panic
