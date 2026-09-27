#pragma once

#include <stdint.h>

namespace block_device {

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    OutOfRange,
    ReadOnly,
    Unsupported,
    NotReady,
    IoError,
};

struct Geometry {
    uint64_t block_count;
    uint32_t block_size;
    bool writable;
};

using ReadFunction = Status (*)(void* context, uint64_t first_block, uint32_t block_count, void* buffer);
using WriteFunction = Status (*)(void* context, uint64_t first_block, uint32_t block_count, const void* buffer);
using FlushFunction = Status (*)(void* context);

/**
 * Describe a block device without taking ownership of its driver state.
 * Callbacks are synchronous and must remain valid for the device's lifetime.
 */
struct Device {
    void* context;
    Geometry geometry;
    ReadFunction read;
    WriteFunction write;
    FlushFunction flush;
};

/**
 * Read complete logical blocks from a device into buffer.
 * The buffer must hold block_count * block_size bytes and remain valid until return.
 */
Status read(Device* device, uint64_t first_block, uint32_t block_count, void* buffer);

/**
 * Write complete logical blocks to a device.
 * The device must be writable and the source buffer must remain valid until return.
 */
Status write(Device* device, uint64_t first_block, uint32_t block_count, const void* buffer);

/**
 * Flush device-side buffered data when supported. The call is synchronous.
 */
Status flush(Device* device);

/** Return the immutable geometry advertised by device, or an empty geometry for null. */
Geometry get_geometry(const Device* device);

} // namespace block_device
