#pragma once

#include "ethernet.hpp"

#include <stdint.h>

namespace arp {

constexpr uint16_t kPacketLength = 28;
constexpr uint16_t kEthernetHardwareType = 1;
constexpr uint16_t kIpv4ProtocolType = 0x0800;
constexpr uint8_t kEthernetAddressLength = 6;
constexpr uint8_t kIpv4AddressLength = 4;
constexpr uint16_t kCacheCapacity = 8;
constexpr uint64_t kDefaultCacheLifetime = 600;

// ARP encodes the opcode as a two-byte network-order field; the wider enum is
// intentional even though the currently supported values fit in one byte.
// NOLINTNEXTLINE(performance-enum-size)
enum class Opcode : uint16_t {
    Request = 1,
    Reply = 2,
};

struct Ipv4Address {
    uint8_t bytes[4];
};

struct PacketView {
    Opcode opcode;
    ethernet::MacAddress sender_hardware;
    Ipv4Address sender_protocol;
    ethernet::MacAddress target_hardware;
    Ipv4Address target_protocol;
};

struct CacheEntry {
    bool valid;
    Ipv4Address address;
    ethernet::MacAddress hardware;
    uint64_t expires_at;
};

struct Cache {
    CacheEntry entries[kCacheCapacity];
};

struct Interface {
    ethernet::MacAddress hardware;
    Ipv4Address protocol;
    Cache cache;
};

/** Compare two IPv4 addresses byte-for-byte. */
bool addresses_equal(Ipv4Address first, Ipv4Address second);

/** Initialize an ARP interface and clear all previously learned mappings. */
void initialize(Interface* interface, ethernet::MacAddress hardware, Ipv4Address protocol);

/** Parse one validated Ethernet/IPv4 ARP payload without copying its data. */
bool parse_packet(const void* data, uint16_t length, PacketView* output);

/** Build an Ethernet/IPv4 ARP request payload. */
bool build_request(const Interface& interface, Ipv4Address target, void* output, uint16_t capacity, uint16_t* length);

/** Build an Ethernet/IPv4 ARP reply payload for a received request. */
bool build_reply(
    const Interface& interface,
    const PacketView& request,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Look up a non-expired IPv4-to-MAC mapping. */
bool cache_lookup(const Cache& cache, Ipv4Address address, uint64_t now, ethernet::MacAddress* hardware);

/** Insert or refresh an IPv4-to-MAC mapping in the fixed-size cache. */
void cache_insert(Cache* cache, Ipv4Address address, ethernet::MacAddress hardware, uint64_t now, uint64_t lifetime);

/**
 * Process an ARP Ethernet frame, updating the cache and optionally producing a
 * complete Ethernet reply frame. Returns true only when a reply was written.
 */
bool process_frame(
    Interface* interface,
    const ethernet::FrameView& frame,
    void* response,
    uint16_t capacity,
    uint16_t* response_length,
    uint64_t now
);

} // namespace arp
