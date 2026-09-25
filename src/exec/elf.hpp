#pragma once

#include <stdint.h>

namespace elf {

constexpr uint8_t kClass64 = 2;
constexpr uint8_t kLittleEndian = 1;
constexpr uint16_t kExecutable = 2;
constexpr uint16_t kMachineX86_64 = 0x3e;
constexpr uint32_t kCurrentVersion = 1;
constexpr uint32_t kLoad = 1;
constexpr uint32_t kProgramHeader = 6;
constexpr uint32_t kInterpreter = 3;
constexpr uint32_t kReadable = 4;
constexpr uint32_t kWritable = 2;
constexpr uint32_t kExecutableFlag = 1;
constexpr uint64_t kIdentitySize = 16;

struct [[gnu::packed]] Header {
    uint8_t identity[kIdentitySize];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t string_table_index;
};

struct [[gnu::packed]] ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

/** Validate an in-memory ELF64 executable without modifying kernel state. */
bool validate(const void* image, uint64_t image_size);

} // namespace elf
