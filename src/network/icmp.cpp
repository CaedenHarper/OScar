#include "icmp.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace icmp {

namespace {

constexpr uint16_t kTypeOffset = 0;
constexpr uint16_t kCodeOffset = 1;
constexpr uint16_t kChecksumOffset = 2;
constexpr uint16_t kIdentifierOffset = 4;
constexpr uint16_t kSequenceOffset = 6;

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

bool is_echo_type(uint8_t type) {
    return type == static_cast<uint8_t>(Type::EchoRequest) || type == static_cast<uint8_t>(Type::EchoReply);
}

} // namespace

bool parse_echo(const void* data, uint16_t length, EchoView* output) {
    if(data == nullptr || output == nullptr || length < kHeaderLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if(!is_echo_type(bytes[kTypeOffset]) || bytes[kCodeOffset] != 0 || ipv4::checksum(data, length) != 0) {
        return false;
    }
    output->type = static_cast<Type>(bytes[kTypeOffset]);
    output->identifier = read_be16(bytes + kIdentifierOffset);
    output->sequence = read_be16(bytes + kSequenceOffset);
    output->payload = bytes + kHeaderLength;
    output->payload_length = static_cast<uint16_t>(length - kHeaderLength);
    return true;
}

Status build_echo_request(
    uint16_t identifier,
    uint16_t sequence,
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
    bytes[kTypeOffset] = static_cast<uint8_t>(Type::EchoRequest);
    bytes[kCodeOffset] = 0;
    write_be16(bytes + kChecksumOffset, 0);
    write_be16(bytes + kIdentifierOffset, identifier);
    write_be16(bytes + kSequenceOffset, sequence);
    if(payload_length != 0) {
        copy_bytes(bytes + kHeaderLength, static_cast<const uint8_t*>(payload), payload_length);
    }
    write_be16(bytes + kChecksumOffset, ipv4::checksum(output, static_cast<uint16_t>(total_length)));
    *length = static_cast<uint16_t>(total_length);
    return Status::Success;
}

Status build_echo_reply(const EchoView& request, void* output, uint16_t capacity, uint16_t* length) {
    if(output == nullptr || length == nullptr || request.payload_length > UINT16_MAX - kHeaderLength) {
        return Status::InvalidArgument;
    }
    const uint16_t total_length = static_cast<uint16_t>(kHeaderLength + request.payload_length);
    if(capacity < total_length) {
        return Status::BufferTooSmall;
    }
    auto* bytes = static_cast<uint8_t*>(output);
    bytes[kTypeOffset] = static_cast<uint8_t>(Type::EchoReply);
    bytes[kCodeOffset] = 0;
    write_be16(bytes + kChecksumOffset, 0);
    write_be16(bytes + kIdentifierOffset, request.identifier);
    write_be16(bytes + kSequenceOffset, request.sequence);
    if(request.payload_length != 0) {
        copy_bytes(bytes + kHeaderLength, request.payload, request.payload_length);
    }
    write_be16(bytes + kChecksumOffset, ipv4::checksum(output, total_length));
    *length = total_length;
    return Status::Success;
}

Status process_echo_request(
    ipv4::Interface& interface,
    const ipv4::PacketView& packet,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t now
) {
    if(output == nullptr || length == nullptr || packet.protocol != ipv4::Protocol::Icmp) {
        return Status::InvalidArgument;
    }
    if(!ipv4::is_for_us(interface, packet)) {
        return Status::Ignored;
    }
    EchoView request;
    if(!parse_echo(packet.payload, packet.payload_length, &request)) {
        return Status::Ignored;
    }
    if(request.type != Type::EchoRequest) {
        return Status::Ignored;
    }
    static uint8_t reply[ipv4::kMaximumPacketLength];
    uint16_t reply_length = 0;
    const Status reply_status = build_echo_reply(request, reply, sizeof(reply), &reply_length);
    if(reply_status != Status::Success) {
        return reply_status;
    }
    const ipv4::FrameStatus frame_status = ipv4::build_frame(
        interface, packet.source, ipv4::Protocol::Icmp, reply, reply_length, output, capacity, length, now
    );
    if(frame_status == ipv4::FrameStatus::Success) {
        return Status::Success;
    }
    if(frame_status == ipv4::FrameStatus::BufferTooSmall) {
        return Status::BufferTooSmall;
    }
    return Status::AddressUnreachable;
}

} // namespace icmp

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
