#pragma once

#include <stdint.h>

namespace virtual_memory {

constexpr uint64_t kPageSize = 4096;

constexpr uint64_t kPresent = 1ULL << 0U;
constexpr uint64_t kWritable = 1ULL << 1U;
constexpr uint64_t kUser = 1ULL << 2U;
constexpr uint64_t kWriteThrough = 1ULL << 3U;
constexpr uint64_t kCacheDisable = 1ULL << 4U;
constexpr uint64_t kNoExecute = 1ULL << 63U;

struct AddressSpace {
    uintptr_t root_physical;
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

/*
 * Create an address space with the current kernel mappings and no user
 * mappings. The physical page allocator and virtual memory manager must be
 * initialized first. Returns false if page-table allocation fails.
 */
bool create_address_space(AddressSpace* address_space);

/*
 * Destroy an address space, releasing its user mappings, user physical pages,
 * intermediate page tables, and root page table. The address space must not
 * be active when this function is called.
 */
void destroy_address_space(AddressSpace* address_space);

/*
 * Map one newly allocated zeroed physical page into a user address space.
 * The address must be page-aligned and below the user-space limit. The page
 * is owned by the address space and is released by unmap_user_page or
 * destroy_address_space.
 */
bool map_user_page(AddressSpace* address_space, uintptr_t virtual_address, uint64_t flags);

/*
 * Remove and release one user mapping, returning its physical address when
 * requested. The address space does not need to be active.
 */
bool unmap_user_page(AddressSpace* address_space, uintptr_t virtual_address, uintptr_t* physical_address);

/*
 * Translate an address using a specific address space without changing the
 * active CPU address space. Returns false when the address is unmapped.
 */
bool translate(const AddressSpace* address_space, uintptr_t virtual_address, uintptr_t* physical_address);

/*
 * Validate that a byte range is accessible from user space in an address
 * space. A zero required_flags value checks readable user pages; including
 * kWritable additionally requires every page to be writable. The range may
 * be unaligned and may span multiple pages. Returns false on overflow,
 * kernel-space addresses, unmapped pages, or insufficient permissions.
 */
bool validate_user_range(
    const AddressSpace* address_space,
    uintptr_t virtual_address,
    uint64_t length,
    uint64_t required_flags
);

/*
 * Load an address space into CR3. The address space must have been created by
 * create_address_space or be the boot address space; this operation returns
 * false for a null or invalid object.
 */
bool activate(const AddressSpace* address_space);

/** Return whether address_space is the currently active translation root. */
bool is_active(const AddressSpace* address_space);

/*
 * Return the address space established by the bootloader and kernel
 * initialization. Its page tables are owned by Limine and must not be
 * destroyed.
 */
const AddressSpace* kernel_address_space();

} // namespace virtual_memory
