#pragma once

#include <stdint.h>

namespace ethernet {

constexpr uint8_t kMacAddressLength = 6;
constexpr uint16_t kHeaderLength = 14;
constexpr uint16_t kMinimumPayloadLength = 46;
constexpr uint16_t kMaximumPayloadLength = 1500;
constexpr uint16_t kMaximumFrameLength = kHeaderLength + kMaximumPayloadLength;

struct MacAddress {
    uint8_t bytes[kMacAddressLength];
};

enum class EtherType : uint16_t {
    Ipv4 = 0x0800,
    Arp = 0x0806,
};

struct FrameView {
    MacAddress destination;
    MacAddress source;
    EtherType type;
    const uint8_t* payload;
    uint16_t payload_length;
};

/**
 * Build an Ethernet frame in output, including required minimum-frame padding.
 * The output remains owned by the caller and must hold up to
 * kMaximumFrameLength bytes.
 */
bool build_frame(
    uint8_t* output,
    uint16_t capacity,
    MacAddress destination,
    MacAddress source,
    EtherType type,
    const void* payload,
    uint16_t payload_length,
    uint16_t* frame_length
);

/** Parse a complete Ethernet frame without copying its payload. */
bool parse_frame(const void* data, uint16_t length, FrameView* output);

/** Compare two MAC addresses. */
bool addresses_equal(MacAddress first, MacAddress second);

/** Return whether an address is the Ethernet broadcast address. */
bool is_broadcast(MacAddress address);

/** Return whether an address is a multicast address. */
bool is_multicast(MacAddress address);

/** Return whether a frame is addressed to local, broadcast, or multicast MAC. */
bool is_for_us(const FrameView& frame, MacAddress local_address);

} // namespace ethernet
