#include "loader.hpp"

#include "elf.hpp"
#include "kernel_heap.hpp"
#include "process.hpp"
#include "process_internal.hpp"
#include "thread.hpp"
#include "vfs.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)

namespace {

constexpr uintptr_t kUserStackTop = 0x00007fffffffe000ULL;
constexpr uint64_t kMaximumExecutableSize = 4ULL * 1024ULL * 1024ULL;

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
    kernel_thread::Thread** output_thread,
    process::Process* parent
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

    if(parent != nullptr) {
        // A spawned program starts in the caller's directory; otherwise a shell
        // could successfully change directory but every child would resolve paths
        // from the kernel's root instead.
        uint32_t index = 0;
        while(parent->working_directory[index] != '\0') {
            process->working_directory[index] = parent->working_directory[index];
            ++index;
        }
        process->working_directory[index] = '\0';
    }

    const auto* header = static_cast<const elf::Header*>(image);
    auto* thread = kernel_thread::create_user(process, header->entry, kUserStackTop);
    if(thread == nullptr) {
        (void)process::destroy(process);
        return false;
    }

    if(parent != nullptr && !process::set_parent(process, parent)) {
        (void)kernel_thread::destroy(thread);
        (void)process::destroy(process);
        return false;
    }

    *output_process = process;
    *output_thread = thread;
    return true;
}

bool load_path(
    const char* path,
    process::Process* parent,
    process::Process** output_process,
    kernel_thread::Thread** output_thread
) {
    if(path == nullptr || output_process == nullptr || output_thread == nullptr) {
        return false;
    }

    vfs::File file = {};
    if(vfs::open(path, vfs::kOpenRead, &file) != vfs::Status::Success || file.node.size == 0 ||
       file.node.size > kMaximumExecutableSize) {
        return false;
    }

    auto* image = static_cast<uint8_t*>(kernel_heap::allocate(file.node.size));
    if(image == nullptr) {
        (void)vfs::close(&file);
        return false;
    }

    uint64_t copied = 0;
    bool read_success = true;
    while(copied < file.node.size) {
        const auto request = static_cast<uint32_t>(file.node.size - copied);
        uint32_t received = 0;
        if(vfs::read(&file, image + copied, request, &received) != vfs::Status::Success || received == 0) {
            read_success = false;
            break;
        }
        copied += received;
    }
    (void)vfs::close(&file);

    if(!read_success || copied != file.node.size) {
        (void)kernel_heap::free(image);
        return false;
    }

    const bool loaded = load(image, copied, output_process, output_thread, parent);
    if(loaded) {
        process::set_image_path(*output_process, path);
    }
    (void)kernel_heap::free(image);
    return loaded;
}

} // namespace loader

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
