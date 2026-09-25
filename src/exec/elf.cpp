#include "elf.hpp"

#include "virtual_memory.hpp"

#include <stdint.h>

namespace {

// ELF images are untrusted byte buffers; these casts and indexed reads are the
// deliberate bounds-checked parser boundary.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)

constexpr uint8_t kElfMagic0 = 0x7f;
constexpr uint8_t kElfMagic1 = 'E';
constexpr uint8_t kElfMagic2 = 'L';
constexpr uint8_t kElfMagic3 = 'F';
constexpr uint8_t kClassIdentityIndex = 4;
constexpr uint8_t kEndianIdentityIndex = 5;
constexpr uintptr_t kUserAddressLimit = 0x0000800000000000ULL;

bool range_in_image(uint64_t offset, uint64_t length, uint64_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

bool add_overflows(uint64_t first, uint64_t second, uint64_t* result) {
    if(result == nullptr || second > UINT64_MAX - first) {
        return true;
    }
    *result = first + second;
    return false;
}

bool is_canonical_user_address(uint64_t address) {
    return address < kUserAddressLimit;
}

} // namespace

namespace elf {

// NOLINTNEXTLINE(readability-function-cognitive-complexity) validation intentionally keeps all rejection rules together
bool validate(const void* image, uint64_t image_size) {
    if(image == nullptr || image_size < sizeof(Header)) {
        return false;
    }

    const auto* header = static_cast<const Header*>(image);
    if(header->identity[0] != kElfMagic0 || header->identity[1] != kElfMagic1 || header->identity[2] != kElfMagic2 ||
       header->identity[3] != kElfMagic3 || header->identity[kClassIdentityIndex] != kClass64 ||
       header->identity[kEndianIdentityIndex] != kLittleEndian || header->type != kExecutable ||
       header->machine != kMachineX86_64 || header->version != kCurrentVersion ||
       header->header_size != sizeof(Header) || header->program_header_size != sizeof(ProgramHeader) ||
       header->program_header_count == 0 ||
       !range_in_image(
           header->program_header_offset,
           static_cast<uint64_t>(header->program_header_count) * header->program_header_size,
           image_size
       )) {
        return false;
    }

    bool has_load_segment = false;
    bool entry_is_executable = false;
    const auto* program_headers =
        reinterpret_cast<const ProgramHeader*>(static_cast<const uint8_t*>(image) + header->program_header_offset);
    for(uint16_t index = 0; index < header->program_header_count; ++index) {
        const ProgramHeader& segment = program_headers[index];
        if(segment.type == kInterpreter || segment.type == kProgramHeader) {
            // Dynamic linking and self-referential program-header mappings are not part of this first loader.
            return false;
        }
        if(segment.type != kLoad) {
            continue;
        }
        if(segment.memory_size == 0 || segment.file_size > segment.memory_size ||
           !range_in_image(segment.offset, segment.file_size, image_size) ||
           (segment.alignment != 0 && segment.alignment != virtual_memory::kPageSize) ||
           (segment.offset % virtual_memory::kPageSize) != (segment.virtual_address % virtual_memory::kPageSize)) {
            return false;
        }

        uint64_t segment_end = 0;
        if(add_overflows(segment.virtual_address, segment.memory_size, &segment_end) ||
           !is_canonical_user_address(segment.virtual_address) || !is_canonical_user_address(segment_end - 1)) {
            return false;
        }
        const uintptr_t first_page = segment.virtual_address & ~(virtual_memory::kPageSize - 1);
        uint64_t last_page_end = 0;
        if(add_overflows(segment_end, virtual_memory::kPageSize - 1, &last_page_end)) {
            return false;
        }
        const uintptr_t last_page = last_page_end & ~(virtual_memory::kPageSize - 1);
        if(first_page >= kUserAddressLimit || last_page >= kUserAddressLimit) {
            return false;
        }
        if((segment.flags & kExecutableFlag) != 0 && header->entry >= segment.virtual_address &&
           header->entry < segment_end) {
            entry_is_executable = true;
        }
        has_load_segment = true;
    }
    return has_load_segment && is_canonical_user_address(header->entry) && entry_is_executable;
}

} // namespace elf

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-constant-array-index)
