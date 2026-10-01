#include "udp.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace udp {

namespace {

constexpr uint16_t kSourcePortOffset = 0;
constexpr uint16_t kDestinationPortOffset = 2;
constexpr uint16_t kLengthOffset = 4;
constexpr uint16_t kChecksumOffset = 6;

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

uint32_t add_words(uint32_t sum, const uint8_t* bytes, uint16_t length) {
    uint16_t index = 0;
    while(index + 1 < length) {
        sum += (static_cast<uint32_t>(bytes[index]) << 8U) | bytes[index + 1];
        index = static_cast<uint16_t>(index + 2);
    }
    if(index < length) {
        sum += static_cast<uint32_t>(bytes[index]) << 8U;
    }
    return sum;
}

uint16_t finish_checksum(uint32_t sum) {
    while((sum >> 16U) != 0) {
        sum = (sum & 0xffffU) + (sum >> 16U);
    }
    const uint16_t result = static_cast<uint16_t>(~sum);
    return result == 0 ? 0xffff : result;
}

uint16_t checksum(arp::Ipv4Address source, arp::Ipv4Address destination, const uint8_t* packet, uint16_t length) {
    uint32_t sum = 0;
    sum = add_words(sum, source.bytes, sizeof(source.bytes));
    sum = add_words(sum, destination.bytes, sizeof(destination.bytes));
    sum += kProtocolNumber;
    sum += length;
    sum = add_words(sum, packet, length);
    return finish_checksum(sum);
}

} // namespace

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
) {
    if(output == nullptr || length == nullptr || (payload_length != 0 && payload == nullptr)) {
        return Status::InvalidArgument;
    }
    const uint32_t total_length = static_cast<uint32_t>(kHeaderLength) + payload_length;
    if(total_length > UINT16_MAX) {
        return Status::InvalidArgument;
    }
    if(capacity < total_length) {
        return Status::BufferTooSmall;
    }

    auto* bytes = static_cast<uint8_t*>(output);
    write_be16(bytes + kSourcePortOffset, source_port);
    write_be16(bytes + kDestinationPortOffset, destination_port);
    write_be16(bytes + kLengthOffset, static_cast<uint16_t>(total_length));
    write_be16(bytes + kChecksumOffset, 0);
    if(payload_length != 0) {
        copy_bytes(bytes + kHeaderLength, static_cast<const uint8_t*>(payload), payload_length);
    }
    write_be16(bytes + kChecksumOffset, checksum(source, destination, bytes, static_cast<uint16_t>(total_length)));
    *length = static_cast<uint16_t>(total_length);
    return Status::Success;
}

bool parse_packet(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    const void* data,
    uint16_t length,
    DatagramView* output
) {
    if(data == nullptr || output == nullptr || length < kHeaderLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint16_t datagram_length = read_be16(bytes + kLengthOffset);
    if(datagram_length < kHeaderLength || datagram_length > length) {
        return false;
    }
    const uint16_t received_checksum = read_be16(bytes + kChecksumOffset);
    if(received_checksum != 0 && checksum(source, destination, bytes, datagram_length) != 0xffff) {
        return false;
    }
    output->source_port = read_be16(bytes + kSourcePortOffset);
    output->destination_port = read_be16(bytes + kDestinationPortOffset);
    output->length = datagram_length;
    output->payload = bytes + kHeaderLength;
    output->payload_length = static_cast<uint16_t>(datagram_length - kHeaderLength);
    return true;
}

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
) {
    static uint8_t packet[ethernet::kMaximumPayloadLength];
    uint16_t packet_length = 0;
    const Status packet_status = build_packet(
        interface.address,
        destination,
        source_port,
        destination_port,
        payload,
        payload_length,
        packet,
        sizeof(packet),
        &packet_length
    );
    if(packet_status != Status::Success) {
        return packet_status;
    }
    const ipv4::FrameStatus frame_status = ipv4::build_frame(
        interface, destination, ipv4::Protocol::Udp, packet, packet_length, output, capacity, length, now
    );
    switch(frame_status) {
        case ipv4::FrameStatus::Success:
            return Status::Success;
        case ipv4::FrameStatus::BufferTooSmall:
            return Status::BufferTooSmall;
        case ipv4::FrameStatus::InvalidArgument:
            return Status::InvalidArgument;
        case ipv4::FrameStatus::AddressUnreachable:
            return Status::AddressUnreachable;
    }
    return Status::InvalidArgument;
}

} // namespace udp

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
