#include "memory.hpp"

#include <limine.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) bitmap indexing is required here

namespace {

constexpr uint64_t kPageSize = 4096;
constexpr uint64_t kMaximumPhysicalAddress = 64ULL * 1024 * 1024 * 1024;
constexpr uint64_t kMaximumPages = kMaximumPhysicalAddress / kPageSize;
constexpr uint64_t kBitsPerBitmapWord = sizeof(uint64_t) * 8;
constexpr uint64_t kBitmapWords = (kMaximumPages + kBitsPerBitmapWord - 1) / kBitsPerBitmapWord;
constexpr uint64_t kFirstAllocatablePage = 1;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) these remain alive permanently and cannot be const
// A set bit means that the page is unavailable or currently allocated.
uint64_t g_page_bitmap[kBitmapWords];
// A set bit means that the page belongs to a Limine usable region.
uint64_t g_usable_bitmap[kBitmapWords];
uint64_t g_total_pages;
uint64_t g_free_pages;
uint64_t g_next_page = kFirstAllocatablePage;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr uint64_t align_up(uint64_t value) {
    return (value + kPageSize - 1) & ~(kPageSize - 1);
}

constexpr uint64_t align_down(uint64_t value) {
    return value & ~(kPageSize - 1);
}

bool valid_page(uintptr_t physical_address) {
    return physical_address != 0 && physical_address < kMaximumPhysicalAddress && (physical_address % kPageSize) == 0;
}

bool page_is_used(uint64_t page) {
    return (g_page_bitmap[page / kBitsPerBitmapWord] & (1ULL << (page % kBitsPerBitmapWord))) != 0;
}

bool page_is_usable(uint64_t page) {
    return (g_usable_bitmap[page / kBitsPerBitmapWord] & (1ULL << (page % kBitsPerBitmapWord))) != 0;
}

void mark_page_used(uint64_t page) {
    g_page_bitmap[page / kBitsPerBitmapWord] |= 1ULL << (page % kBitsPerBitmapWord);
}

void mark_page_free(uint64_t page) {
    g_page_bitmap[page / kBitsPerBitmapWord] &= ~(1ULL << (page % kBitsPerBitmapWord));
}

void mark_page_usable(uint64_t page) {
    g_usable_bitmap[page / kBitsPerBitmapWord] |= 1ULL << (page % kBitsPerBitmapWord);
}

} // namespace

namespace physical_memory {

void initialize(const limine_memmap_response* memory_map) {
    for(uint64_t word = 0; word < kBitmapWords; ++word) {
        g_page_bitmap[word] = ~0ULL;
        g_usable_bitmap[word] = 0;
    }

    g_total_pages = 0;
    g_free_pages = 0;
    g_next_page = kFirstAllocatablePage;

    for(uint64_t entry_index = 0; entry_index < memory_map->entry_count; ++entry_index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) we are managing memory here, so we must
        const limine_memmap_entry* entry = memory_map->entries[entry_index];
        if(entry->type != LIMINE_MEMMAP_USABLE || entry->base >= kMaximumPhysicalAddress) {
            continue;
        }

        const uint64_t region_end = entry->base > UINT64_MAX - entry->length ? UINT64_MAX : entry->base + entry->length;
        const uint64_t first_page_address = align_up(entry->base < kPageSize ? kPageSize : entry->base);
        const uint64_t last_page_address =
            align_down(region_end < kMaximumPhysicalAddress ? region_end : kMaximumPhysicalAddress);

        for(uint64_t address = first_page_address; address < last_page_address; address += kPageSize) {
            const uint64_t page = address / kPageSize;
            if(page_is_used(page)) {
                mark_page_usable(page);
                mark_page_free(page);
                ++g_total_pages;
                ++g_free_pages;
            }
        }
    }
}

bool allocate_page(uintptr_t* physical_address) {
    if(physical_address == nullptr || g_free_pages == 0) {
        return false;
    }

    for(uint64_t offset = 0; offset < kMaximumPages; ++offset) {
        const uint64_t page = (g_next_page + offset) % kMaximumPages;
        if(!page_is_used(page)) {
            mark_page_used(page);
            --g_free_pages;
            g_next_page = (page + 1) % kMaximumPages;
            *physical_address = page * kPageSize;
            return true;
        }
    }

    return false;
}

bool free_page(uintptr_t physical_address) {
    if(!valid_page(physical_address)) {
        return false;
    }

    const uint64_t page = physical_address / kPageSize;
    if(!page_is_usable(page) || !page_is_used(page)) {
        return false;
    }

    mark_page_free(page);
    ++g_free_pages;
    if(page < g_next_page) {
        g_next_page = page;
    }
    return true;
}

uint64_t total_pages() {
    return g_total_pages;
}

uint64_t free_pages() {
    return g_free_pages;
}

} // namespace physical_memory

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index) this file uses page tables which require array
