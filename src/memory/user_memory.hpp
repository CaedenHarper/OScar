#pragma once

#include <stdint.h>

namespace user_memory {

/** Validate a current-process user range without copying data. */
bool validate(uintptr_t user_address, uint64_t length, bool writable);

/** Copy bytes from the current process's validated user address space. */
bool copy_from_user(void* kernel_destination, uintptr_t user_source, uint64_t length);

/** Copy bytes into the current process's validated user address space. */
bool copy_to_user(uintptr_t user_destination, const void* kernel_source, uint64_t length);

} // namespace user_memory
