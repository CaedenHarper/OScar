#include "kernel_heap.hpp"

#include "memory.hpp"
#include "virtual_memory.hpp"

// NOLINTBEGIN(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic) Heap code must use pointer
// arithmetic, and must convert ints to pointers

namespace {

constexpr uint64_t kPageSize = virtual_memory::kPageSize;
constexpr uint64_t kAlignment = 16;
constexpr uintptr_t kHeapBase = 0xffff900000000000ULL;
constexpr uintptr_t kHeapLimit = kHeapBase + (1024ULL * 1024 * 1024);

struct Block {
    uint64_t size;
    bool free;
    Block* next;
    Block* previous;
};

static_assert(sizeof(Block) % kAlignment == 0);

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) these remain alive permanently and cannot be const
Block* g_first_block;
Block* g_last_block;
uintptr_t g_heap_end;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

uint64_t align_up(uint64_t value) {
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

bool grow(uint64_t minimum_bytes) {
    if(minimum_bytes > kHeapLimit - kHeapBase - sizeof(Block)) {
        return false;
    }

    const uint64_t required_pages = (minimum_bytes + sizeof(Block) + kPageSize - 1) / kPageSize;
    const uint64_t bytes = required_pages * kPageSize;
    if(bytes == 0 || g_heap_end > kHeapLimit - bytes) {
        return false;
    }

    uint64_t mapped_count = 0;
    for(; mapped_count < required_pages; ++mapped_count) {
        uintptr_t physical_page = 0;
        if(!physical_memory::allocate_page(&physical_page)) {
            for(uint64_t index = 0; index < mapped_count; ++index) {
                uintptr_t unmapped_page = 0;
                if(virtual_memory::unmap_page(g_heap_end + (index * kPageSize), &unmapped_page)) {
                    physical_memory::free_page(unmapped_page);
                }
            }
            return false;
        }

        if(!virtual_memory::map_page(
               g_heap_end + (mapped_count * kPageSize),
               physical_page,
               virtual_memory::kWritable | virtual_memory::kNoExecute
           )) {
            physical_memory::free_page(physical_page);
            for(uint64_t index = 0; index < mapped_count; ++index) {
                uintptr_t unmapped_page = 0;
                if(virtual_memory::unmap_page(g_heap_end + (index * kPageSize), &unmapped_page)) {
                    physical_memory::free_page(unmapped_page);
                }
            }
            return false;
        }
    }

    auto* block = reinterpret_cast<Block*>(g_heap_end);
    block->size = bytes - sizeof(Block);
    block->free = true;
    block->next = nullptr;
    block->previous = g_last_block;
    if(g_last_block != nullptr) {
        g_last_block->next = block;
    } else {
        g_first_block = block;
    }
    g_last_block = block;
    g_heap_end += bytes;
    return true;
}

void split_block(Block* block, uint64_t size) {
    const uint64_t remaining = block->size - size;
    if(remaining < sizeof(Block) + kAlignment) {
        return;
    }

    auto* remainder = reinterpret_cast<Block*>(reinterpret_cast<uintptr_t>(block + 1) + size);
    remainder->size = remaining - sizeof(Block);
    remainder->free = true;
    remainder->next = block->next;
    remainder->previous = block;
    if(block->next != nullptr) {
        block->next->previous = remainder;
    } else {
        g_last_block = remainder;
    }
    block->next = remainder;
    block->size = size;
}

void merge_with_next(Block* block) {
    Block const* next = block->next;
    if(next == nullptr || !next->free) {
        return;
    }

    block->size += sizeof(Block) + next->size;
    block->next = next->next;
    if(block->next != nullptr) {
        block->next->previous = block;
    } else {
        g_last_block = block;
    }
}

} // namespace

namespace kernel_heap {

void initialize() {
    g_first_block = nullptr;
    g_last_block = nullptr;
    g_heap_end = kHeapBase;
}

void* allocate(uint64_t size) {
    if(size == 0) {
        return nullptr;
    }
    size = align_up(size);

    for(;;) {
        for(Block* block = g_first_block; block != nullptr; block = block->next) {
            if(block->free && block->size >= size) {
                split_block(block, size);
                block->free = false;
                return block + 1;
            }
        }

        if(!grow(size)) {
            return nullptr;
        }
    }
}

bool free(void* pointer) {
    if(pointer == nullptr) {
        return false;
    }

    const auto address = reinterpret_cast<uintptr_t>(pointer);
    if(address < kHeapBase + sizeof(Block) || address >= g_heap_end ||
       (address - (kHeapBase + sizeof(Block))) % kAlignment != 0) {
        return false;
    }

    Block* block = reinterpret_cast<Block*>(pointer) - 1;
    if(block->free || block->size == 0 || block->size % kAlignment != 0) {
        return false;
    }

    block->free = true;
    merge_with_next(block);
    if(block->previous != nullptr && block->previous->free) {
        merge_with_next(block->previous);
    }
    return true;
}

} // namespace kernel_heap

// NOLINTEND(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic)
