#pragma once

#include "block_device.hpp"

namespace virtio_block {

/** Initialize the first legacy-compatible VirtIO PCI block device. */
bool initialize();

/** Return the initialized VirtIO block device, or nullptr before initialization. */
block_device::Device* device();

} // namespace virtio_block
