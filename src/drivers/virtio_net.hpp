#pragma once

#include <stdint.h>

namespace virtio_net {

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    NotReady,
    IoError,
};

struct MacAddress {
    uint8_t bytes[6];
};

struct Device {
    void* context;
    MacAddress mac;
    Status (*send)(void* context, const void* frame, uint16_t length);
    Status (*receive)(void* context, void* frame, uint16_t capacity, uint16_t* length);
};

/** Initialize the first legacy-compatible VirtIO network PCI function. */
bool initialize();

/** Return the initialized network device, or nullptr before initialization. */
Device* device();

} // namespace virtio_net
