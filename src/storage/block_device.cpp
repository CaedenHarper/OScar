#include "block_device.hpp"

#include <stdint.h>

namespace block_device {

namespace {} // namespace

Status read(Device* device, uint64_t first_block, uint32_t block_count, void* buffer) {
    if(device == nullptr || block_count == 0 || buffer == nullptr || device->geometry.block_size == 0 ||
       device->geometry.block_count == 0) {
        return Status::InvalidArgument;
    }
    if(device->read == nullptr) {
        return Status::Unsupported;
    }
    if(first_block >= device->geometry.block_count ||
       static_cast<uint64_t>(block_count) > device->geometry.block_count - first_block) {
        return Status::OutOfRange;
    }
    return device->read(device->context, first_block, block_count, buffer);
}

Status write(Device* device, uint64_t first_block, uint32_t block_count, const void* buffer) {
    if(device == nullptr || block_count == 0 || buffer == nullptr || device->geometry.block_size == 0 ||
       device->geometry.block_count == 0) {
        return Status::InvalidArgument;
    }
    if(!device->geometry.writable) {
        return Status::ReadOnly;
    }
    if(device->write == nullptr) {
        return Status::Unsupported;
    }
    if(first_block >= device->geometry.block_count ||
       static_cast<uint64_t>(block_count) > device->geometry.block_count - first_block) {
        return Status::OutOfRange;
    }
    return device->write(device->context, first_block, block_count, buffer);
}

Status flush(Device* device) {
    if(device == nullptr || device->geometry.block_size == 0 || device->geometry.block_count == 0) {
        return Status::InvalidArgument;
    }
    if(device->flush == nullptr) {
        return Status::Unsupported;
    }
    return device->flush(device->context);
}

Geometry get_geometry(const Device* device) {
    return device == nullptr ? Geometry{} : device->geometry;
}

} // namespace block_device
