#include "block_device.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             readability-math-missing-parentheses, bugprone-easily-swappable-parameters, misc-const-correctness)
namespace {

constexpr uint32_t kTestBlockSize = 16;
constexpr uint64_t kTestBlockCount = 4;
constexpr uint8_t kWritePattern = 0xa5;

struct MemoryDevice {
    uint8_t bytes[kTestBlockSize * kTestBlockCount];
    bool flushed;
};

block_device::Status read_memory(void* context, uint64_t first_block, uint32_t block_count, void* buffer) {
    auto* device = static_cast<MemoryDevice*>(context);
    auto* destination = static_cast<uint8_t*>(buffer);
    for(uint32_t block = 0; block < block_count; ++block) {
        for(uint32_t byte = 0; byte < kTestBlockSize; ++byte) {
            destination[block * kTestBlockSize + byte] = device->bytes[(first_block + block) * kTestBlockSize + byte];
        }
    }
    return block_device::Status::Success;
}

block_device::Status write_memory(void* context, uint64_t first_block, uint32_t block_count, const void* buffer) {
    auto* device = static_cast<MemoryDevice*>(context);
    const auto* source = static_cast<const uint8_t*>(buffer);
    for(uint32_t block = 0; block < block_count; ++block) {
        for(uint32_t byte = 0; byte < kTestBlockSize; ++byte) {
            device->bytes[(first_block + block) * kTestBlockSize + byte] = source[block * kTestBlockSize + byte];
        }
    }
    return block_device::Status::Success;
}

block_device::Status flush_memory(void* context) {
    static_cast<MemoryDevice*>(context)->flushed = true;
    return block_device::Status::Success;
}

} // namespace

void test_block_device() {
    MemoryDevice memory = {};
    block_device::Device device = {
        &memory,
        {kTestBlockCount, kTestBlockSize, true},
        read_memory,
        write_memory,
        flush_memory,
    };
    uint8_t write_buffer[kTestBlockSize * 2] = {};
    uint8_t read_buffer[kTestBlockSize * 2] = {};
    for(uint8_t& byte : write_buffer) {
        byte = kWritePattern;
    }

    if(block_device::write(&device, 1, 2, write_buffer) != block_device::Status::Success ||
       block_device::read(&device, 1, 2, read_buffer) != block_device::Status::Success) {
        panic::halt("block-device smoke test could not read and write complete blocks");
    }
    for(const uint8_t byte : read_buffer) {
        if(byte != kWritePattern) {
            panic::halt("block-device smoke test read back incorrect data");
        }
    }

    if(block_device::read(&device, kTestBlockCount, 1, read_buffer) != block_device::Status::OutOfRange ||
       block_device::write(&device, 3, 2, write_buffer) != block_device::Status::OutOfRange ||
       block_device::read(&device, 0, 1, nullptr) != block_device::Status::InvalidArgument ||
       block_device::flush(&device) != block_device::Status::Success || !memory.flushed) {
        panic::halt("block-device smoke test accepted an invalid request");
    }

    device.geometry.writable = false;
    if(block_device::write(&device, 0, 1, write_buffer) != block_device::Status::ReadOnly) {
        panic::halt("block-device smoke test wrote to a read-only device");
    }
    serial::write("Block-device protocol smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-math-missing-parentheses, bugprone-easily-swappable-parameters, misc-const-correctness)
