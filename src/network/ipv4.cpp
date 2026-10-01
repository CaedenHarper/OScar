#include "ipv4.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace ipv4 {

namespace {

constexpr uint16_t kVersionAndHeaderOffset = 0;
constexpr uint16_t kTotalLengthOffset = 2;
constexpr uint16_t kIdentificationOffset = 4;
constexpr uint16_t kFlagsAndOffsetOffset = 6;
constexpr uint16_t kTtlOffset = 8;
constexpr uint16_t kProtocolOffset = 9;
constexpr uint16_t kChecksumOffset = 10;
constexpr uint16_t kSourceOffset = 12;
constexpr uint16_t kDestinationOffset = 16;
constexpr uint16_t kFragmentOffsetMask = 0x1fff;
constexpr uint16_t kMoreFragmentsFlag = 0x2000;

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

uint32_t address_value(arp::Ipv4Address address) {
    return (static_cast<uint32_t>(address.bytes[0]) << 24U) | (static_cast<uint32_t>(address.bytes[1]) << 16U) |
           (static_cast<uint32_t>(address.bytes[2]) << 8U) | address.bytes[3];
}

void write_address(uint8_t* output, arp::Ipv4Address address) {
    copy_bytes(output, address.bytes, sizeof(address.bytes));
}

arp::Ipv4Address read_address(const uint8_t* input) {
    arp::Ipv4Address address;
    copy_bytes(address.bytes, input, sizeof(address.bytes));
    return address;
}

} // namespace

uint16_t checksum(const void* data, uint16_t length) {
    if(data == nullptr) {
        return 0;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;
    uint16_t index = 0;
    while(index + 1 < length) {
        sum += (static_cast<uint32_t>(bytes[index]) << 8U) | bytes[index + 1];
        index = static_cast<uint16_t>(index + 2);
    }
    if(index < length) {
        sum += static_cast<uint32_t>(bytes[index]) << 8U;
    }
    while((sum >> 16U) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    return static_cast<uint16_t>(~sum);
}

void initialize(
    Interface* interface,
    arp::Interface* arp_interface,
    arp::Ipv4Address address,
    arp::Ipv4Address netmask,
    arp::Ipv4Address gateway
) {
    if(interface == nullptr) {
        return;
    }
    interface->arp_interface = arp_interface;
    interface->address = address;
    interface->netmask = netmask;
    interface->gateway = gateway;
    interface->next_identification = 0;
}

FrameStatus build_packet(
    Interface& interface,
    arp::Ipv4Address destination,
    Protocol protocol,
    const void* payload,
    uint16_t payload_length,
    void* output,
    uint16_t capacity,
    uint16_t* length
) {
    if(output == nullptr || length == nullptr || (payload_length != 0 && payload == nullptr)) {
        return FrameStatus::InvalidArgument;
    }
    const uint32_t total_length = static_cast<uint32_t>(kMinimumHeaderLength) + payload_length;
    if(total_length > kMaximumPacketLength) {
        return FrameStatus::InvalidArgument;
    }
    if(capacity < total_length) {
        return FrameStatus::BufferTooSmall;
    }

    auto* bytes = static_cast<uint8_t*>(output);
    bytes[kVersionAndHeaderOffset] = static_cast<uint8_t>(kVersion << 4U) | 5;
    bytes[1] = 0;
    write_be16(bytes + kTotalLengthOffset, static_cast<uint16_t>(total_length));
    write_be16(bytes + kIdentificationOffset, interface.next_identification);
    write_be16(bytes + kFlagsAndOffsetOffset, kDontFragment);
    bytes[kTtlOffset] = kDefaultTtl;
    bytes[kProtocolOffset] = static_cast<uint8_t>(protocol);
    write_be16(bytes + kChecksumOffset, 0);
    write_address(bytes + kSourceOffset, interface.address);
    write_address(bytes + kDestinationOffset, destination);
    write_be16(bytes + kChecksumOffset, checksum(bytes, kMinimumHeaderLength));
    if(payload_length != 0) {
        copy_bytes(bytes + kMinimumHeaderLength, static_cast<const uint8_t*>(payload), payload_length);
    }
    ++interface.next_identification;
    *length = static_cast<uint16_t>(total_length);
    return FrameStatus::Success;
}

bool parse_packet(const void* data, uint16_t length, PacketView* output) {
    if(data == nullptr || output == nullptr || length < kMinimumHeaderLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint8_t version = static_cast<uint8_t>(bytes[0] >> 4U);
    const uint8_t header_words = bytes[0] & 0x0fU;
    const uint8_t header_length = static_cast<uint8_t>(header_words * 4U);
    const uint16_t total_length = read_be16(bytes + kTotalLengthOffset);
    const uint16_t flags_and_offset = read_be16(bytes + kFlagsAndOffsetOffset);
    if(version != kVersion || header_words < 5 || header_length > length || total_length < header_length ||
       total_length > length || checksum(bytes, header_length) != 0 ||
       (flags_and_offset & (kMoreFragmentsFlag | kFragmentOffsetMask)) != 0) {
        return false;
    }
    output->header_length = header_length;
    output->total_length = total_length;
    output->identification = read_be16(bytes + kIdentificationOffset);
    output->flags_and_offset = flags_and_offset;
    output->ttl = bytes[kTtlOffset];
    output->protocol = static_cast<Protocol>(bytes[kProtocolOffset]);
    output->source = read_address(bytes + kSourceOffset);
    output->destination = read_address(bytes + kDestinationOffset);
    output->payload = bytes + header_length;
    output->payload_length = static_cast<uint16_t>(total_length - header_length);
    return true;
}

bool is_same_subnet(arp::Ipv4Address first, arp::Ipv4Address second, arp::Ipv4Address netmask) {
    return (address_value(first) & address_value(netmask)) == (address_value(second) & address_value(netmask));
}

bool route(const Interface& interface, arp::Ipv4Address destination, Route* output) {
    if(output == nullptr) {
        return false;
    }
    if(arp::addresses_equal(destination, interface.address)) {
        output->kind = RouteKind::Local;
        output->next_hop = destination;
        return true;
    }
    if(is_same_subnet(interface.address, destination, interface.netmask)) {
        output->kind = RouteKind::Direct;
        output->next_hop = destination;
        return true;
    }
    if(address_value(interface.gateway) == 0) {
        return false;
    }
    output->kind = RouteKind::Gateway;
    output->next_hop = interface.gateway;
    return true;
}

bool is_for_us(const Interface& interface, const PacketView& packet) {
    return arp::addresses_equal(packet.destination, interface.address);
}

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
) {
    if(interface.arp_interface == nullptr || output == nullptr || length == nullptr) {
        return FrameStatus::InvalidArgument;
    }
    Route selected_route;
    if(!route(interface, destination, &selected_route) || selected_route.kind == RouteKind::Local) {
        return FrameStatus::AddressUnreachable;
    }
    ethernet::MacAddress next_hop_hardware;
    if(!arp::cache_lookup(interface.arp_interface->cache, selected_route.next_hop, now, &next_hop_hardware)) {
        return FrameStatus::AddressUnreachable;
    }
    static uint8_t packet[ethernet::kMaximumPayloadLength];
    uint16_t packet_length = 0;
    const FrameStatus packet_status =
        build_packet(interface, destination, protocol, payload, payload_length, packet, sizeof(packet), &packet_length);
    if(packet_status != FrameStatus::Success) {
        return packet_status;
    }
    if(!ethernet::build_frame(
           static_cast<uint8_t*>(output),
           capacity,
           next_hop_hardware,
           interface.arp_interface->hardware,
           ethernet::EtherType::Ipv4,
           packet,
           packet_length,
           length
       )) {
        return FrameStatus::BufferTooSmall;
    }
    return FrameStatus::Success;
}

} // namespace ipv4

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
