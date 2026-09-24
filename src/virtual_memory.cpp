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

constexpr unsigned kPageOffsetBits = 12U;
constexpr unsigned kPageTableIndexBits = 9U;
constexpr unsigned kPageTableIndexMask = kEntriesPerTable - 1U;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) must outlive initialization and cannot be const
uintptr_t g_hhdm_offset;

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
        uint64_t* new_table = nullptr;
        if(!allocate_table(&new_table)) {
            return nullptr;
        }

        const uintptr_t physical_address = reinterpret_cast<uintptr_t>(new_table) - g_hhdm_offset;
        entry = physical_address | table_flags(flags);
    } else if((entry & kLargePage) != 0) {
        return nullptr;
    }

    return table_from_physical(entry & kEntryAddressMask);
}

unsigned page_table_index(uintptr_t virtual_address, unsigned level) {
    return (virtual_address >> (kPageOffsetBits + (kPageTableIndexBits * level))) & kPageTableIndexMask;
}

uint64_t* find_page_table(uintptr_t virtual_address) {
    uint64_t* table = table_from_physical(read_cr3());
    for(int level = 3; level > 0; --level) {
        const uint64_t entry = table[page_table_index(virtual_address, level)];
        if((entry & kEntryPresent) == 0 || (entry & kLargePage) != 0) {
            return nullptr;
        }
        table = table_from_physical(entry & kEntryAddressMask);
    }
    return table;
}

void invalidate(uintptr_t virtual_address) {
    asm volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

} // namespace

namespace virtual_memory {

void initialize(uintptr_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;
}

bool map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags) {
    if((virtual_address % kPageSize) != 0 || (physical_address % kPageSize) != 0) {
        return false;
    }

    uint64_t* table = table_from_physical(read_cr3());
    for(int level = 3; level > 0; --level) {
        table = next_table(table, page_table_index(virtual_address, level), flags);
        if(table == nullptr) {
            return false;
        }
    }

    uint64_t& entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) != 0) {
        return false;
    }

    entry = physical_address | (flags & ~kEntryAddressMask);
    entry |= kEntryPresent;
    invalidate(virtual_address);
    return true;
}

bool unmap_page(uintptr_t virtual_address, uintptr_t* physical_address) {
    if(physical_address == nullptr || (virtual_address % kPageSize) != 0) {
        return false;
    }

    uint64_t* table = find_page_table(virtual_address);
    if(table == nullptr) {
        return false;
    }

    uint64_t& entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) == 0) {
        return false;
    }

    *physical_address = entry & kEntryAddressMask;
    entry = 0;
    invalidate(virtual_address);
    return true;
}

bool translate(uintptr_t virtual_address, uintptr_t* physical_address) {
    if(physical_address == nullptr) {
        return false;
    }

    const uint64_t* table = find_page_table(virtual_address);
    if(table == nullptr) {
        return false;
    }

    const uint64_t entry = table[page_table_index(virtual_address, 0)];
    if((entry & kEntryPresent) == 0) {
        return false;
    }

    *physical_address = (entry & kEntryAddressMask) + (virtual_address & (kPageSize - 1));
    return true;
}

} // namespace virtual_memory

// NOLINTEND(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic)
