#include "loader.hpp"

#include "elf.hpp"
#include "process.hpp"
#include "thread.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)

namespace {

constexpr uintptr_t kUserStackTop = 0x00007fffffffe000ULL;

uint64_t segment_flags(const elf::ProgramHeader& segment) {
    uint64_t flags = virtual_memory::kNoExecute;
    if((segment.flags & elf::kExecutableFlag) != 0) {
        flags &= ~virtual_memory::kNoExecute;
    }
    if((segment.flags & elf::kWritable) != 0) {
        flags |= virtual_memory::kWritable;
    }
    return flags;
}

bool copy_segment(process::Process* process, const uint8_t* image, const elf::ProgramHeader& segment) {
    auto* address_space = process::address_space(process);
    uint64_t copied = 0;
    while(copied < segment.memory_size) {
        const uintptr_t virtual_address = segment.virtual_address + copied;
        uintptr_t physical_address = 0;
        if(!virtual_memory::translate(address_space, virtual_address, &physical_address)) {
            return false;
        }

        auto* destination = static_cast<uint8_t*>(virtual_memory::direct_map(physical_address));
        const uint64_t page_remaining = virtual_memory::kPageSize - (physical_address % virtual_memory::kPageSize);
        const uint64_t remaining_memory = segment.memory_size - copied;
        const uint64_t chunk = remaining_memory < page_remaining ? remaining_memory : page_remaining;
        for(uint64_t index = 0; index < chunk; ++index) {
            const uint64_t image_offset = copied + index;
            // Bytes beyond p_filesz are .bss and must be zero even when the physical page was reused.
            destination[index] = image_offset < segment.file_size ? image[segment.offset + image_offset] : 0;
        }
        copied += chunk;
    }
    return true;
}

bool load_segments(process::Process* process, const void* image) {
    const auto* header = static_cast<const elf::Header*>(image);
    const auto* program_headers =
        reinterpret_cast<const elf::ProgramHeader*>(static_cast<const uint8_t*>(image) + header->program_header_offset);
    auto* address_space = process::address_space(process);
    for(uint16_t index = 0; index < header->program_header_count; ++index) {
        const auto& segment = program_headers[index];
        if(segment.type != elf::kLoad) {
            continue;
        }
        const uintptr_t first_page = segment.virtual_address & ~(virtual_memory::kPageSize - 1);
        const uintptr_t segment_end = segment.virtual_address + segment.memory_size;
        const uintptr_t last_page = (segment_end + virtual_memory::kPageSize - 1) & ~(virtual_memory::kPageSize - 1);
        for(uintptr_t page = first_page; page < last_page; page += virtual_memory::kPageSize) {
            if(!virtual_memory::map_user_page(address_space, page, segment_flags(segment))) {
                return false;
            }
        }
        if(!copy_segment(process, static_cast<const uint8_t*>(image), segment)) {
            return false;
        }
    }
    return virtual_memory::map_user_page(
        address_space, kUserStackTop - virtual_memory::kPageSize, virtual_memory::kWritable | virtual_memory::kNoExecute
    );
}

} // namespace

namespace loader {

bool load(
    const void* image,
    uint64_t image_size,
    process::Process** output_process,
    kernel_thread::Thread** output_thread
) {
    if(output_process == nullptr || output_thread == nullptr || !elf::validate(image, image_size)) {
        return false;
    }

    auto* process = process::create();
    if(process == nullptr || !load_segments(process, image)) {
        if(process != nullptr) {
            (void)process::destroy(process);
        }
        return false;
    }

    const auto* header = static_cast<const elf::Header*>(image);
    auto* thread = kernel_thread::create_user(process, header->entry, kUserStackTop);
    if(thread == nullptr) {
        (void)process::destroy(process);
        return false;
    }

    *output_process = process;
    *output_thread = thread;
    return true;
}

} // namespace loader

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
