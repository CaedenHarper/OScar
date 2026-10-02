#pragma once

#include "arp.hpp"

#include <stdint.h>

namespace tcp {

constexpr uint16_t kHeaderLength = 20;
constexpr uint8_t kProtocolNumber = 6;
constexpr uint8_t kFin = 0x01;
constexpr uint8_t kSyn = 0x02;
constexpr uint8_t kRst = 0x04;
constexpr uint8_t kPsh = 0x08;
constexpr uint8_t kAck = 0x10;
constexpr uint8_t kUrg = 0x20;
// Each supported flag occupies a distinct bit, so addition expresses the mask
// without relying on integral promotion of the uint8_t constants.
constexpr uint8_t kSupportedFlags = kFin + kSyn + kRst + kPsh + kAck + kUrg;

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
};

struct SegmentView {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t flags;
    uint16_t window;
    uint16_t urgent_pointer;
    const uint8_t* payload;
    uint16_t payload_length;
};

/** Build a checksummed TCP segment without IPv4 or Ethernet framing. */
Status build_segment(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    uint16_t source_port,
    uint16_t destination_port,
    uint32_t sequence,
    uint32_t acknowledgment,
    uint8_t flags,
    uint16_t window,
    uint16_t urgent_pointer,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Parse and checksum-validate a TCP segment carried by IPv4. */
bool parse_segment(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    const void* data,
    uint16_t length,
    SegmentView* output
);

} // namespace tcp
