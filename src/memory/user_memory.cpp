#include "user_memory.hpp"

#include "scheduler.hpp"
#include "thread.hpp"
#include "thread_internal.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace {

bool copy(void* kernel_address, uintptr_t user_address, const void* source, uint64_t length, bool to_user) {
    auto* thread = scheduler::current();
    auto* address_space = thread == nullptr ? nullptr : thread->address_space;
    if(address_space == nullptr || !virtual_memory::validate_user_range(
                                       address_space, user_address, length, to_user ? virtual_memory::kWritable : 0
                                   )) {
        return false;
    }

    // A zero-length transfer has no buffer to dereference. Accepting it keeps the
    // syscall ABI consistent with validate() and avoids rejecting harmless calls with
    // a null buffer that the caller never intends to access.
    if(length == 0) {
        return true;
    }
    if(kernel_address == nullptr || source == nullptr) {
        return false;
    }

    auto* destination_bytes = static_cast<uint8_t*>(kernel_address);
    const auto* source_bytes = static_cast<const uint8_t*>(source);
    uint64_t copied = 0;
    while(copied < length) {
        uintptr_t physical_address = 0;
        if(!virtual_memory::translate(address_space, user_address + copied, &physical_address)) {
            return false;
        }
        auto* user_bytes = static_cast<uint8_t*>(virtual_memory::direct_map(physical_address));
        const uint64_t page_remaining = virtual_memory::kPageSize - (physical_address % virtual_memory::kPageSize);
        const uint64_t chunk = length - copied < page_remaining ? length - copied : page_remaining;
        for(uint64_t index = 0; index < chunk; ++index) {
            if(to_user) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) the validated page is byte-addressed
                user_bytes[index] = source_bytes[copied + index];
            } else {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) the validated page is byte-addressed
                destination_bytes[copied + index] = user_bytes[index];
            }
        }
        copied += chunk;
    }
    return true;
}

} // namespace

namespace user_memory {

bool validate(uintptr_t user_address, uint64_t length, bool writable) {
    auto* thread = scheduler::current();
    auto* address_space = thread == nullptr ? nullptr : thread->address_space;
    return address_space != nullptr && virtual_memory::validate_user_range(
                                           address_space, user_address, length, writable ? virtual_memory::kWritable : 0
                                       );
}

bool copy_from_user(void* kernel_destination, uintptr_t user_source, uint64_t length) {
    return copy(kernel_destination, user_source, kernel_destination, length, false);
}

bool copy_to_user(uintptr_t user_destination, const void* kernel_source, uint64_t length) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr) the pointer is only an opaque non-null marker
    void* opaque_destination = reinterpret_cast<void*>(user_destination);
    return copy(opaque_destination, user_destination, kernel_source, length, true);
}

} // namespace user_memory
