#pragma once

#include <stdint.h>

namespace virtual_memory {

constexpr uint64_t kPageSize = 4096;

enum PageFlags : uint64_t {
    kPresent = 1ULL << 0,
    kWritable = 1ULL << 1,
    kUser = 1ULL << 2,
    kWriteThrough = 1ULL << 3,
    kCacheDisable = 1ULL << 4,
    kNoExecute = 1ULL << 63,
};

/*
 * Initialize the virtual memory manager using Limine's higher-half direct
 * map. The existing page tables remain active and are not replaced.
 */
void initialize(uintptr_t hhdm_offset);

/*
 * Map one 4 KiB virtual page to a physical page using the supplied flags.
 * Returns false if the virtual page is already mapped or allocation fails.
 */
bool map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags);

/*
 * Remove one 4 KiB mapping and return its physical address through the output
 * argument. The physical page itself remains allocated to the caller.
 */
bool unmap_page(uintptr_t virtual_address, uintptr_t* physical_address);

/*
 * Translate a virtual address using the active page tables. The returned
 * physical address includes the original address's offset within its page.
 */
bool translate(uintptr_t virtual_address, uintptr_t* physical_address);

} // namespace virtual_memory
