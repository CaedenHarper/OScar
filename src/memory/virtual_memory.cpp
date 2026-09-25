#include "virtual_memory.hpp"

#include "memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic) pointer arithmetic is
// required for memory management

namespace {

constexpr uint64_t kEntryPresent = 1ULL << 0U;
constexpr uint64_t kEntryWritable = 1ULL << 1U;
constexpr uint64_t kEntryUser = 1ULL << 2U;
constexpr uint64_t kEntryAddressMask = 0x000ffffffffff000ULL;
constexpr uint64_t kLargePage = 1ULL << 7U;
constexpr unsigned kEntriesPerTable = 512;
constexpr unsigned kKernelPml4Index = 256;
constexpr uintptr_t kUserAddressLimit = 0x0000800000000000ULL;

constexpr unsigned kPageOffsetBits = 12U;
constexpr unsigned kPageTableIndexBits = 9U;
constexpr unsigned kPageTableIndexMask = kEntriesPerTable - 1U;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) must outlive initialization and cannot be const
uintptr_t g_hhdm_offset;
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) these track the active address space
virtual_memory::AddressSpace g_kernel_address_space = {};
virtual_memory::AddressSpace g_active_address_space = {};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uintptr_t read_cr3() {
    // NOLINTNEXTLINE(misc-const-correctness) ASM writes
    uintptr_t value = 0;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value & kEntryAddressMask;
}

uintptr_t physical_to_virtual(uintptr_t physical_address) {
    return g_hhdm_offset + physical_address;
}

uint64_t* table_from_physical(uintptr_t physical_address) {
    return reinterpret_cast<uint64_t*>(physical_to_virtual(physical_address));
}

void clear_page(uintptr_t physical_address) {
    uint64_t* page = table_from_physical(physical_address);
    for(unsigned index = 0; index < kEntriesPerTable; ++index) {
        page[index] = 0;
    }
}

bool allocate_table(uint64_t** table) {
    uintptr_t physical_address = 0;
    if(!physical_memory::allocate_page(&physical_address)) {
        return false;
    }

    clear_page(physical_address);
    *table = table_from_physical(physical_address);
    return true;
}

uint64_t table_flags(uint64_t flags) {
    uint64_t result = kEntryPresent | kEntryWritable;
    if((flags & virtual_memory::kUser) != 0) {
        result |= kEntryUser;
    }
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
uint64_t* next_table(uint64_t* table, unsigned index, uint64_t flags) {
    uint64_t& entry = table[index];
    if((entry & kEntryPresent) == 0) {
        // NOLINTNEXTLINE(misc-const-correctness) allocate_table initializes this output pointer
        uint64_t* new_table = nullptr;
        if(!allocate_table(&new_table)) {
            return nullptr;
        }

        const uintptr_t physical_address = reinterpret_cast<uintptr_t>(new_table) - g_hhdm_offset;
        entry = physical_address | table_flags(flags);
    } else if((entry & kLargePage) != 0) {
        // This mapper intentionally supports 4 KiB leaves only; refusing to descend
        // through a large-page entry avoids silently replacing a larger mapping.
        return nullptr;
    }

    return table_from_physical(entry & kEntryAddressMask);
}

unsigned page_table_index(uintptr_t virtual_address, unsigned level) {
    return (virtual_address >> (kPageOffsetBits + (kPageTableIndexBits * level))) & kPageTableIndexMask;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
uint64_t* find_page_table(uintptr_t root_physical, uintptr_t virtual_address) {
    // NOLINTNEXTLINE(misc-const-correctness) the mutable result is used by unmap_page_in_root
    uint64_t* table = table_from_physical(root_physical);
    for(int level = 3; level > 0; --level) {
        const uint64_t entry = table[page_table_index(virtual_address, level)];
        if((entry & kEntryPresent) == 0 || (entry & kLargePage) != 0) {
            return nullptr;
        }
        table = table_from_physical(entry & kEntryAddressMask);
    }
    return table;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool map_page_in_root(uintptr_t root_physical, uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags) {
    if((virtual_address % virtual_memory::kPageSize) != 0 || (physical_address % virtual_memory::kPageSize) != 0) {
        return false;
    }

    uint64_t* table = table_from_physical(root_physical);
    for(int level = 3; level > 0; --level) {
        table = next_table(table, page_table_index(virtual_address, level), flags);
        if(table == nullptr) {
            return false;
        }
    }

    uint64_t& entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) != 0) {
        // Refuse remapping rather than discarding the old physical page, since ownership
        // of that page belongs to the caller of unmap_user_page or unmap_page.
        return false;
    }

    entry = physical_address | (flags & ~kEntryAddressMask);
    entry |= kEntryPresent;
    return true;
}

bool unmap_page_in_root(uintptr_t root_physical, uintptr_t virtual_address, uintptr_t* physical_address) {
    if(physical_address == nullptr || (virtual_address % virtual_memory::kPageSize) != 0) {
        return false;
    }

    uint64_t* table = find_page_table(root_physical, virtual_address);
    if(table == nullptr) {
        return false;
    }

    uint64_t& entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) == 0) {
        return false;
    }

    *physical_address = entry & kEntryAddressMask;
    entry = 0;
    return true;
}

bool translate_in_root(uintptr_t root_physical, uintptr_t virtual_address, uintptr_t* physical_address) {
    if(physical_address == nullptr) {
        return false;
    }

    const uint64_t* table = find_page_table(root_physical, virtual_address);
    if(table == nullptr) {
        return false;
    }

    const uint64_t entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) == 0) {
        return false;
    }

    *physical_address = (entry & kEntryAddressMask) + (virtual_address & (virtual_memory::kPageSize - 1));
    return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool page_is_accessible(uintptr_t root_physical, uintptr_t virtual_address, uint64_t required_flags) {
    const uint64_t* table = find_page_table(root_physical, virtual_address);
    if(table == nullptr) {
        return false;
    }

    const uint64_t entry = table[page_table_index(virtual_address, 0)];
    return (entry & kEntryPresent) != 0 && (entry & kEntryUser) != 0 && (entry & required_flags) == required_flags;
}

// NOLINTNEXTLINE(misc-no-recursion, bugprone-easily-swappable-parameters)
void destroy_user_table(uintptr_t table_physical, unsigned level) {
    const uint64_t* const table = table_from_physical(table_physical);
    for(unsigned index = 0; index < kEntriesPerTable; ++index) {
        const uint64_t entry = table[index];
        if((entry & kEntryPresent) == 0) {
            continue;
        }

        const uintptr_t physical_address = entry & kEntryAddressMask;
        if(level == 0 || (entry & kLargePage) != 0) {
            physical_memory::free_page(physical_address);
        } else {
            destroy_user_table(physical_address, level - 1);
            physical_memory::free_page(physical_address);
        }
    }
}

void invalidate(uintptr_t virtual_address) {
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

} // namespace

namespace virtual_memory {

void initialize(uintptr_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;
    g_kernel_address_space.root_physical = read_cr3();
    g_active_address_space = g_kernel_address_space;
}

bool map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags) {
    if(!map_page_in_root(g_active_address_space.root_physical, virtual_address, physical_address, flags)) {
        return false;
    }

    invalidate(virtual_address);
    return true;
}

bool unmap_page(uintptr_t virtual_address, uintptr_t* physical_address) {
    if(!unmap_page_in_root(g_active_address_space.root_physical, virtual_address, physical_address)) {
        return false;
    }

    invalidate(virtual_address);
    return true;
}

bool translate(uintptr_t virtual_address, uintptr_t* physical_address) {
    return translate_in_root(g_active_address_space.root_physical, virtual_address, physical_address);
}

void* direct_map(uintptr_t physical_address) {
    return reinterpret_cast<void*>(physical_to_virtual(physical_address));
}

bool create_address_space(AddressSpace* address_space) {
    if(address_space == nullptr || g_kernel_address_space.root_physical == 0) {
        return false;
    }

    uintptr_t root_physical = 0;
    if(!physical_memory::allocate_page(&root_physical)) {
        return false;
    }

    clear_page(root_physical);
    uint64_t* root = table_from_physical(root_physical);
    const uint64_t* kernel_root = table_from_physical(g_kernel_address_space.root_physical);
    // Share the higher-half kernel mappings while leaving all lower-half entries private
    // to the new address space; later process support can populate those entries safely.
    for(unsigned index = kKernelPml4Index; index < kEntriesPerTable; ++index) {
        root[index] = kernel_root[index];
    }

    address_space->root_physical = root_physical;
    return true;
}

void destroy_address_space(AddressSpace* address_space) {
    // Never free a root that is globally owned or currently active; callers must switch
    // away before destroying an address space.
    if(address_space == nullptr || address_space->root_physical == 0 ||
       address_space->root_physical == g_kernel_address_space.root_physical ||
       address_space->root_physical == g_active_address_space.root_physical) {
        return;
    }

    // NOLINTNEXTLINE(misc-const-correctness) this root receives copied kernel entries
    uint64_t* const root = table_from_physical(address_space->root_physical);
    for(unsigned index = 0; index < kKernelPml4Index; ++index) {
        const uint64_t entry = root[index];
        if((entry & kEntryPresent) == 0) {
            continue;
        }

        destroy_user_table(entry & kEntryAddressMask, 2);
        physical_memory::free_page(entry & kEntryAddressMask);
    }

    physical_memory::free_page(address_space->root_physical);
    address_space->root_physical = 0;
}

bool map_user_page(AddressSpace* address_space, uintptr_t virtual_address, uint64_t flags) {
    if(address_space == nullptr || address_space->root_physical == 0 || virtual_address >= kUserAddressLimit ||
       (virtual_address % kPageSize) != 0) {
        return false;
    }

    uintptr_t physical_address = 0;
    if(!physical_memory::allocate_page(&physical_address)) {
        return false;
    }

    clear_page(physical_address);
    if(!map_page_in_root(address_space->root_physical, virtual_address, physical_address, flags | kUser)) {
        physical_memory::free_page(physical_address);
        return false;
    }

    if(address_space->root_physical == g_active_address_space.root_physical) {
        invalidate(virtual_address);
    }
    return true;
}

bool unmap_user_page(AddressSpace* address_space, uintptr_t virtual_address, uintptr_t* physical_address) {
    if(address_space == nullptr || address_space->root_physical == 0 || virtual_address >= kUserAddressLimit) {
        return false;
    }

    uintptr_t mapped_physical_address = 0;
    if(!unmap_page_in_root(address_space->root_physical, virtual_address, &mapped_physical_address)) {
        return false;
    }

    if(!physical_memory::free_page(mapped_physical_address)) {
        return false;
    }

    if(physical_address != nullptr) {
        *physical_address = mapped_physical_address;
    }
    if(address_space->root_physical == g_active_address_space.root_physical) {
        invalidate(virtual_address);
    }
    return true;
}

bool translate(const AddressSpace* address_space, uintptr_t virtual_address, uintptr_t* physical_address) {
    if(address_space == nullptr || address_space->root_physical == 0) {
        return false;
    }
    return translate_in_root(address_space->root_physical, virtual_address, physical_address);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) length and flags intentionally use the same integer type
bool validate_user_range(
    const AddressSpace* address_space,
    uintptr_t virtual_address,
    uint64_t length,
    uint64_t required_flags
) {
    if(address_space == nullptr || address_space->root_physical == 0 || virtual_address >= kUserAddressLimit) {
        return false;
    }
    // Empty buffers are valid without touching page tables, while non-empty buffers are
    // checked page by page so unaligned endpoints are covered by their containing pages.
    if(length == 0) {
        return true;
    }
    if(length > kUserAddressLimit - virtual_address) {
        return false;
    }

    const uintptr_t last_address = virtual_address + length - 1;
    const uintptr_t first_page = virtual_address & ~(kPageSize - 1);
    const uintptr_t last_page = last_address & ~(kPageSize - 1);
    for(uintptr_t page = first_page; page <= last_page; page += kPageSize) {
        if(!page_is_accessible(address_space->root_physical, page, required_flags | kUser)) {
            return false;
        }
    }
    return true;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

bool activate(const AddressSpace* address_space) {
    if(address_space == nullptr || address_space->root_physical == 0) {
        return false;
    }

    // Loading CR3 changes the translation root and flushes the relevant TLB state; mirror
    // it in software only after the instruction succeeds.
    asm volatile("mov %0, %%cr3" : : "r"(address_space->root_physical) : "memory");
    g_active_address_space = *address_space;
    return true;
}

bool is_active(const AddressSpace* address_space) {
    return address_space != nullptr && address_space->root_physical != 0 &&
           address_space->root_physical == g_active_address_space.root_physical;
}

const AddressSpace* kernel_address_space() {
    return &g_kernel_address_space;
}

} // namespace virtual_memory

// NOLINTEND(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic)
