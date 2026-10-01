#pragma once

#include "arp.hpp"
#include "ethernet.hpp"

#include <stdint.h>

namespace ipv4 {

constexpr uint16_t kMinimumHeaderLength = 20;
constexpr uint16_t kMaximumPacketLength = 65535;
constexpr uint8_t kVersion = 4;
constexpr uint8_t kDefaultTtl = 64;
constexpr uint16_t kDontFragment = 0x4000;

enum class Protocol : uint8_t {
    Icmp = 1,
    Tcp = 6,
    Udp = 17,
};

enum class RouteKind : uint8_t {
    Local,
    Direct,
    Gateway,
};

enum class FrameStatus : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
    AddressUnreachable,
};

struct PacketView {
    uint8_t header_length;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_and_offset;
    uint8_t ttl;
    Protocol protocol;
    arp::Ipv4Address source;
    arp::Ipv4Address destination;
    const uint8_t* payload;
    uint16_t payload_length;
};

struct Interface {
    arp::Interface* arp_interface;
    arp::Ipv4Address address;
    arp::Ipv4Address netmask;
    arp::Ipv4Address gateway;
    uint16_t next_identification;
};

struct Route {
    RouteKind kind;
    arp::Ipv4Address next_hop;
};

/** Calculate the Internet checksum for an IPv4 header or pseudo-header. */
uint16_t checksum(const void* data, uint16_t length);

/** Initialize IPv4 configuration and associate it with an ARP interface. */
void initialize(
    Interface* interface,
    arp::Interface* arp_interface,
    arp::Ipv4Address address,
    arp::Ipv4Address netmask,
    arp::Ipv4Address gateway
);

/** Build a validated, unfragmented IPv4 packet without Ethernet framing. */
FrameStatus build_packet(
    Interface& interface,
    arp::Ipv4Address destination,
    Protocol protocol,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Parse and validate an IPv4 packet without copying its payload. */
bool parse_packet(const void* data, uint16_t length, PacketView* output);

/** Determine whether an IPv4 destination is on the configured local subnet. */
bool is_same_subnet(arp::Ipv4Address first, arp::Ipv4Address second, arp::Ipv4Address netmask);

/** Select local delivery, direct delivery, or the configured gateway. */
bool route(const Interface& interface, arp::Ipv4Address destination, Route* output);

/** Return whether a parsed packet is addressed to this interface. */
bool is_for_us(const Interface& interface, const PacketView& packet);

/**
 * Build an IPv4 packet and Ethernet frame using the ARP cache for next-hop
 * resolution. No ARP request is sent when the cache does not contain a MAC.
 */
FrameStatus build_frame(
    Interface& interface,
    arp::Ipv4Address destination,
    Protocol protocol,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t now
);

} // namespace ipv4
