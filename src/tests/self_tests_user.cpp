#include "elf.hpp"
#include "loader.hpp"
#include "process.hpp"
#include "scheduler.hpp"
#include "serial.hpp"
#include "thread.hpp"
#include "virtual_memory.hpp"

extern "C" const uint8_t user_program_start[];
extern "C" const uint8_t user_program_end[];

#include "self_tests_internal.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference)
// User-mode, synthetic ELF, and embedded ELF tests.

bool write_user_bytes(
    virtual_memory::AddressSpace* address_space,
    uintptr_t virtual_address,
    const uint8_t* bytes,
    uint64_t length
) {
    uintptr_t physical_address = 0;
    if(!virtual_memory::translate(address_space, virtual_address, &physical_address)) {
        return false;
    }
    auto* destination = static_cast<uint8_t*>(virtual_memory::direct_map(physical_address));
    const uint64_t offset = physical_address % virtual_memory::kPageSize;
    if(offset + length > virtual_memory::kPageSize) {
        return false;
    }
    for(uint64_t index = 0; index < length; ++index) {
        destination[index] = bytes[index];
    }
    return true;
}

bool prepare_user_test_thread() {
    constexpr char kMessage[] = "User-mode syscall smoke test passed.\n";
    auto* user_process = process::create();
    auto* address_space = user_process == nullptr ? nullptr : process::address_space(user_process);
    if(address_space == nullptr || !virtual_memory::map_user_page(address_space, kUserTestCodeAddress, 0) ||
       !virtual_memory::map_user_page(
           address_space, kUserTestMessageAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       ) ||
       !virtual_memory::map_user_page(
           address_space, kUserTestStackAddress, virtual_memory::kWritable | virtual_memory::kNoExecute
       )) {
        return false;
    }

    uint8_t code[] = {
        0x48,
        0xb8,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x48,
        0xbf,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0x48,
        0xbe,
        sizeof(kMessage) - 1,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0xcd,
        0x80,
        0x48,
        0xb8,
        0x01,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0xcd,
        0x80,
        0xeb,
        0xfe,
    };
    const uint64_t message_address = kUserTestMessageAddress;
    for(unsigned index = 0; index < sizeof(message_address); ++index) {
        code[12 + index] = static_cast<uint8_t>(message_address >> (index * 8U));
    }
    if(!write_user_bytes(address_space, kUserTestCodeAddress, code, sizeof(code)) ||
       !write_user_bytes(
           address_space, kUserTestMessageAddress, reinterpret_cast<const uint8_t*>(kMessage), sizeof(kMessage) - 1
       )) {
        return false;
    }

    auto* user_thread = kernel_thread::create_user(
        user_process, kUserTestCodeAddress, kUserTestStackAddress + virtual_memory::kPageSize
    );
    return user_thread != nullptr && scheduler::enqueue(user_thread);
}

bool prepare_elf_test_thread() {
    constexpr uintptr_t kEntry = 0x400100;
    constexpr uintptr_t kMessageAddress = kEntry + 0x40;
    constexpr uint64_t kProgramOffset = 0x100;
    constexpr uint64_t kMessageOffset = 0x40;
    constexpr char kMessage[] = "ELF loader smoke test passed.\n";
    constexpr uint64_t kImageSize = 512;
    alignas(8) static uint8_t image[kImageSize] = {};

    auto* header = reinterpret_cast<elf::Header*>(image);
    header->identity[0] = 0x7f;
    header->identity[1] = 'E';
    header->identity[2] = 'L';
    header->identity[3] = 'F';
    header->identity[4] = elf::kClass64;
    header->identity[5] = elf::kLittleEndian;
    header->type = elf::kExecutable;
    header->machine = elf::kMachineX86_64;
    header->version = elf::kCurrentVersion;
    header->entry = kEntry;
    header->program_header_offset = sizeof(elf::Header);
    header->header_size = sizeof(elf::Header);
    header->program_header_size = sizeof(elf::ProgramHeader);
    header->program_header_count = 1;

    auto* program_header = reinterpret_cast<elf::ProgramHeader*>(image + sizeof(elf::Header));
    program_header->type = elf::kLoad;
    program_header->flags = elf::kReadable | elf::kExecutableFlag;
    program_header->offset = kProgramOffset;
    program_header->virtual_address = kEntry;
    program_header->file_size = kMessageOffset + sizeof(kMessage) - 1;
    program_header->memory_size = virtual_memory::kPageSize;
    program_header->alignment = virtual_memory::kPageSize;

    uint8_t code[] = {
        0xb8, 0, 0, 0,    0,    0x48, 0xbf, 0, 0, 0, 0,    0,    0,    0,    0, 0xbe, sizeof(kMessage) - 1,
        0,    0, 0, 0xcd, 0x80, 0xb8, 0x01, 0, 0, 0, 0xcd, 0x80, 0xeb, 0xfe,
    };
    for(uint64_t index = 0; index < sizeof(code); ++index) {
        image[kProgramOffset + index] = code[index];
    }
    for(uint64_t index = 0; index < sizeof(kMessageAddress); ++index) {
        image[kProgramOffset + 7 + index] = static_cast<uint8_t>(kMessageAddress >> (index * 8U));
    }
    for(uint64_t index = 0; index < sizeof(kMessage) - 1; ++index) {
        image[kProgramOffset + kMessageOffset + index] = static_cast<uint8_t>(kMessage[index]);
    }

    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    if(!loader::load(image, sizeof(image), &process, &thread) || process == nullptr || thread == nullptr ||
       !scheduler::enqueue(thread)) {
        return false;
    }
    serial::write("ELF loader process prepared.\n");
    return true;
}

bool prepare_real_elf_test_thread() {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(user_program_end - user_program_start);
    if(!loader::load(user_program_start, image_size, &process, &thread) || process == nullptr || thread == nullptr ||
       !scheduler::enqueue(thread)) {
        return false;
    }
    serial::write("Real ELF process prepared.\n");
    return true;
}

bool prepare_embedded_elf_thread(const uint8_t* image, const uint8_t* image_end) {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(image_end - image);
    if(!loader::load(image, image_size, &process, &thread) || process == nullptr || thread == nullptr) {
        return false;
    }
    return scheduler::enqueue(thread);
}

bool prepare_crash_test_thread(const uint8_t* image, const uint8_t* image_end) {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(image_end - image);
    if(!loader::load(image, image_size, &process, &thread) || process == nullptr || thread == nullptr) {
        return false;
    }
    return scheduler::enqueue(thread);
}

bool prepare_init_process() {
    process::Process* process = nullptr;
    kernel_thread::Thread* thread = nullptr;
    const uint64_t image_size = static_cast<uint64_t>(user_program_init_end - user_program_init_start);
    if(!loader::load(user_program_init_start, image_size, &process, &thread) || process == nullptr ||
       thread == nullptr) {
        return false;
    }
    return scheduler::enqueue(thread);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)

} // namespace self_tests_detail
