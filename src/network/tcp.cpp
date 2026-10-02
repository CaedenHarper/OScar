#include "tcp.hpp"

#include <stdint.h>

// TCP packet construction intentionally uses explicit wire offsets and byte
// pointers. Keeping this layer stateless makes it reusable by future sockets
// and keeps connection sequencing out of the packet codec.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace tcp {

namespace {

constexpr uint16_t kSourcePortOffset = 0;
constexpr uint16_t kDestinationPortOffset = 2;
constexpr uint16_t kSequenceOffset = 4;
constexpr uint16_t kAcknowledgmentOffset = 8;
constexpr uint16_t kDataOffsetOffset = 12;
constexpr uint16_t kFlagsOffset = 13;
constexpr uint16_t kWindowOffset = 14;
constexpr uint16_t kChecksumOffset = 16;
constexpr uint16_t kUrgentPointerOffset = 18;
constexpr uint8_t kDataOffsetWords = 5;
constexpr uint8_t kDataOffsetShift = 4;
constexpr uint8_t kReservedBitsMask = 0x0f;
constexpr uint8_t kMaximumDataOffsetWords = 15;

void copy_bytes(uint8_t* destination, const uint8_t* source, uint16_t length) {
    for(uint16_t index = 0; index < length; ++index) {
        destination[index] = source[index];
    }
}

uint16_t read_be16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

uint32_t read_be32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) | (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) | bytes[3];
}

void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

void write_be32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 24U);
    bytes[1] = static_cast<uint8_t>(value >> 16U);
    bytes[2] = static_cast<uint8_t>(value >> 8U);
    bytes[3] = static_cast<uint8_t>(value);
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
    return static_cast<uint16_t>(~sum);
}

uint16_t checksum(arp::Ipv4Address source, arp::Ipv4Address destination, const uint8_t* segment, uint16_t length) {
    uint32_t sum = 0;
    sum = add_words(sum, source.bytes, sizeof(source.bytes));
    sum = add_words(sum, destination.bytes, sizeof(destination.bytes));
    sum += kProtocolNumber;
    // The word accumulator operates on values, while add_words() handles the
    // byte order of serialized fields. TCP's pseudo-header length is already
    // a 16-bit word value here; swapping it would make external peers reject
    // otherwise valid packets even though our own parser would agree with the
    // same incorrect calculation.
    sum += length;
    sum = add_words(sum, segment, length);
    return finish_checksum(sum);
}

bool valid_flags(uint8_t flags) {
    if((flags & static_cast<uint8_t>(~kSupportedFlags)) != 0) {
        return false;
    }
    // SYN and FIN cannot describe the same segment. RST also cannot be used
    // with SYN because a reset rejects a connection rather than establishing it.
    return (flags & kSyn) == 0 || (flags & kFin) == 0;
}

} // namespace

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
) {
    if(output == nullptr || length == nullptr || !valid_flags(flags) || (payload_length != 0 && payload == nullptr)) {
        return Status::InvalidArgument;
    }
    const uint32_t total_length = static_cast<uint32_t>(kHeaderLength) + payload_length;
    if(total_length > UINT16_MAX || capacity < total_length) {
        return total_length > UINT16_MAX ? Status::InvalidArgument : Status::BufferTooSmall;
    }

    auto* bytes = static_cast<uint8_t*>(output);
    write_be16(bytes + kSourcePortOffset, source_port);
    write_be16(bytes + kDestinationPortOffset, destination_port);
    write_be32(bytes + kSequenceOffset, sequence);
    write_be32(bytes + kAcknowledgmentOffset, acknowledgment);
    bytes[kDataOffsetOffset] = static_cast<uint8_t>(kDataOffsetWords << kDataOffsetShift);
    bytes[kFlagsOffset] = flags;
    write_be16(bytes + kWindowOffset, window);
    write_be16(bytes + kChecksumOffset, 0);
    write_be16(bytes + kUrgentPointerOffset, urgent_pointer);
    if(payload_length != 0) {
        copy_bytes(bytes + kHeaderLength, static_cast<const uint8_t*>(payload), payload_length);
    }
    write_be16(bytes + kChecksumOffset, checksum(source, destination, bytes, static_cast<uint16_t>(total_length)));
    *length = static_cast<uint16_t>(total_length);
    return Status::Success;
}

bool parse_segment(
    arp::Ipv4Address source,
    arp::Ipv4Address destination,
    const void* data,
    uint16_t length,
    SegmentView* output
) {
    if(data == nullptr || output == nullptr || length < kHeaderLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint8_t data_offset_words = static_cast<uint8_t>(bytes[kDataOffsetOffset] >> kDataOffsetShift);
    if(data_offset_words < kDataOffsetWords || data_offset_words > kMaximumDataOffsetWords) {
        return false;
    }
    const uint16_t header_length = static_cast<uint16_t>(data_offset_words * 4U);
    if(header_length > length || (bytes[kDataOffsetOffset] & kReservedBitsMask) != 0 ||
       !valid_flags(bytes[kFlagsOffset]) || checksum(source, destination, bytes, length) != 0) {
        return false;
    }

    output->source_port = read_be16(bytes + kSourcePortOffset);
    output->destination_port = read_be16(bytes + kDestinationPortOffset);
    output->sequence = read_be32(bytes + kSequenceOffset);
    output->acknowledgment = read_be32(bytes + kAcknowledgmentOffset);
    output->flags = bytes[kFlagsOffset];
    output->window = read_be16(bytes + kWindowOffset);
    output->urgent_pointer = read_be16(bytes + kUrgentPointerOffset);
    output->payload = bytes + header_length;
    output->payload_length = static_cast<uint16_t>(length - header_length);
    return true;
}

} // namespace tcp

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
