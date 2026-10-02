#pragma once

#include "arp.hpp"
#include "ipv4.hpp"

#include <stdint.h>

namespace udp {

constexpr uint16_t kHeaderLength = 8;
constexpr uint8_t kProtocolNumber = 17;

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
    AddressUnreachable,
};

struct DatagramView {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    const uint8_t* payload;
    uint16_t payload_length;
};

/** Build a UDP datagram and calculate its IPv4 UDP checksum. */
Status build_packet(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    uint16_t source_port,
    uint16_t destination_port,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Parse and validate a UDP datagram carried by the supplied IPv4 addresses. */
bool parse_packet(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    const void* data,
    uint16_t length,
    DatagramView* output
);

/** Build a UDP datagram inside a routed Ethernet/IPv4 frame. */
Status build_frame(
    ipv4::Interface& interface,
    arp::Ipv4Address destination,
    uint16_t source_port,
    uint16_t destination_port,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t now
);

} // namespace udp
