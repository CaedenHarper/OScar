#pragma once

#include "ipv4.hpp"

#include <stdint.h>

namespace icmp {

constexpr uint16_t kHeaderLength = 8;

enum class Type : uint8_t {
    EchoReply = 0,
    EchoRequest = 8,
};

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
    Ignored,
    AddressUnreachable,
};

struct EchoView {
    Type type;
    uint16_t identifier;
    uint16_t sequence;
    const uint8_t* payload;
    uint16_t payload_length;
};

/** Parse and validate an ICMP echo request or reply. */
bool parse_echo(const void* data, uint16_t length, EchoView* output);

/** Build an ICMP echo request, including its checksum. */
Status build_echo_request(
    uint16_t identifier,
    uint16_t sequence,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Build an ICMP echo reply while preserving the request identity and payload. */
Status build_echo_reply(const EchoView& request, void* output, uint16_t capacity, uint16_t* length);

/**
 * Handle a local IPv4 echo request and build a complete Ethernet reply frame.
 * The destination MAC must already be present in the ARP cache.
 */
Status process_echo_request(
    ipv4::Interface& interface,
    const ipv4::PacketView& packet,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t now
);

} // namespace icmp
