#include "arp.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace arp {

namespace {

constexpr uint16_t kSenderHardwareOffset = 8;
constexpr uint16_t kSenderProtocolOffset = kSenderHardwareOffset + kEthernetAddressLength;
constexpr uint16_t kTargetHardwareOffset = kSenderProtocolOffset + kIpv4AddressLength;
constexpr uint16_t kTargetProtocolOffset = kTargetHardwareOffset + kEthernetAddressLength;

void copy_bytes(uint8_t* destination, const uint8_t* source, uint16_t length) {
    for(uint16_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
}

uint16_t read_be16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

void write_packet(
    uint8_t* bytes,
    Opcode opcode,
    ethernet::MacAddress sender_hardware,
    Ipv4Address sender_protocol,
    ethernet::MacAddress target_hardware,
    Ipv4Address target_protocol
) {
    write_be16(bytes, kEthernetHardwareType);
    write_be16(bytes + 2, kIpv4ProtocolType);
    bytes[4] = kEthernetAddressLength;
    bytes[5] = kIpv4AddressLength;
    write_be16(bytes + 6, static_cast<uint16_t>(opcode));
    copy_bytes(bytes + kSenderHardwareOffset, sender_hardware.bytes, kEthernetAddressLength);
    copy_bytes(bytes + kSenderProtocolOffset, sender_protocol.bytes, kIpv4AddressLength);
    copy_bytes(bytes + kTargetHardwareOffset, target_hardware.bytes, kEthernetAddressLength);
    copy_bytes(bytes + kTargetProtocolOffset, target_protocol.bytes, kIpv4AddressLength);
}

bool valid_packet_header(const uint8_t* bytes) {
    return read_be16(bytes) == kEthernetHardwareType && read_be16(bytes + 2) == kIpv4ProtocolType &&
           bytes[4] == kEthernetAddressLength && bytes[5] == kIpv4AddressLength;
}

bool entry_expired(const CacheEntry& entry, uint64_t now) {
    return !entry.valid || now >= entry.expires_at;
}

} // namespace

bool addresses_equal(Ipv4Address first, Ipv4Address second) {
    for(uint8_t index = 0; index < kIpv4AddressLength; ++index) {
        if(first.bytes[index] != second.bytes[index]) {
            return false;
        }
    }
    return true;
}

void initialize(Interface* interface, ethernet::MacAddress hardware, Ipv4Address protocol) {
    if(interface == nullptr) {
        return;
    }
    interface->hardware = hardware;
    interface->protocol = protocol;
    for(CacheEntry& entry : interface->cache.entries) {
        entry = {};
    }
}

bool parse_packet(const void* data, uint16_t length, PacketView* output) {
    if(data == nullptr || output == nullptr || length < kPacketLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if(!valid_packet_header(bytes)) {
        return false;
    }
    const uint16_t opcode = read_be16(bytes + 6);
    if(opcode != static_cast<uint16_t>(Opcode::Request) && opcode != static_cast<uint16_t>(Opcode::Reply)) {
        return false;
    }
    output->opcode = static_cast<Opcode>(opcode);
    copy_bytes(output->sender_hardware.bytes, bytes + kSenderHardwareOffset, kEthernetAddressLength);
    copy_bytes(output->sender_protocol.bytes, bytes + kSenderProtocolOffset, kIpv4AddressLength);
    copy_bytes(output->target_hardware.bytes, bytes + kTargetHardwareOffset, kEthernetAddressLength);
    copy_bytes(output->target_protocol.bytes, bytes + kTargetProtocolOffset, kIpv4AddressLength);
    return true;
}

bool build_request(const Interface& interface, Ipv4Address target, void* output, uint16_t capacity, uint16_t* length) {
    if(output == nullptr || length == nullptr || capacity < kPacketLength) {
        return false;
    }
    static constexpr ethernet::MacAddress kUnknownHardware = {{0, 0, 0, 0, 0, 0}};
    write_packet(
        static_cast<uint8_t*>(output), Opcode::Request, interface.hardware, interface.protocol, kUnknownHardware, target
    );
    *length = kPacketLength;
    return true;
}

bool build_reply(
    const Interface& interface,
    const PacketView& request,
    void* output,
    uint16_t capacity,
    uint16_t* length
) {
    if(output == nullptr || length == nullptr || capacity < kPacketLength) {
        return false;
    }
    write_packet(
        static_cast<uint8_t*>(output),
        Opcode::Reply,
        interface.hardware,
        interface.protocol,
        request.sender_hardware,
        request.sender_protocol
    );
    *length = kPacketLength;
    return true;
}

bool cache_lookup(const Cache& cache, Ipv4Address address, uint64_t now, ethernet::MacAddress* hardware) {
    if(hardware == nullptr) {
        return false;
    }
    for(const CacheEntry& entry : cache.entries) {
        if(!entry_expired(entry, now) && addresses_equal(entry.address, address)) {
            *hardware = entry.hardware;
            return true;
        }
    }
    return false;
}

void cache_insert(Cache* cache, Ipv4Address address, ethernet::MacAddress hardware, uint64_t now, uint64_t lifetime) {
    if(cache == nullptr) {
        return;
    }
    CacheEntry* replacement = nullptr;
    for(CacheEntry& entry : cache->entries) {
        if(!entry_expired(entry, now) && addresses_equal(entry.address, address)) {
            replacement = &entry;
            break;
        }
        if(replacement == nullptr && entry_expired(entry, now)) {
            replacement = &entry;
        }
    }
    if(replacement == nullptr) {
        replacement = &cache->entries[0];
        for(CacheEntry& entry : cache->entries) {
            if(entry.expires_at < replacement->expires_at) {
                replacement = &entry;
            }
        }
    }
    replacement->valid = true;
    replacement->address = address;
    replacement->hardware = hardware;
    replacement->expires_at = lifetime > UINT64_MAX - now ? UINT64_MAX : now + lifetime;
}

bool process_frame(
    Interface* interface,
    const ethernet::FrameView& frame,
    void* response,
    uint16_t capacity,
    uint16_t* response_length,
    uint64_t now
) {
    if(interface == nullptr || frame.type != ethernet::EtherType::Arp || frame.payload == nullptr ||
       response == nullptr || response_length == nullptr) {
        return false;
    }
    PacketView packet = {};
    if(!parse_packet(frame.payload, frame.payload_length, &packet)) {
        return false;
    }
    cache_insert(&interface->cache, packet.sender_protocol, packet.sender_hardware, now);
    if(packet.opcode != Opcode::Request || !addresses_equal(packet.target_protocol, interface->protocol)) {
        return false;
    }
    static uint8_t reply_payload[kPacketLength];
    uint16_t reply_payload_length = 0;
    if(!build_reply(*interface, packet, reply_payload, sizeof(reply_payload), &reply_payload_length)) {
        return false;
    }
    return ethernet::build_frame(
        static_cast<uint8_t*>(response),
        capacity,
        packet.sender_hardware,
        interface->hardware,
        ethernet::EtherType::Arp,
        reply_payload,
        reply_payload_length,
        response_length
    );
}

} // namespace arp

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
