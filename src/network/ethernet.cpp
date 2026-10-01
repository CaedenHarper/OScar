#include "ethernet.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace ethernet {

namespace {

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

uint16_t ether_type_value(EtherType type) {
    return static_cast<uint16_t>(type);
}

} // namespace

bool build_frame(
    uint8_t* output,
    uint16_t capacity,
    MacAddress destination,
    MacAddress source,
    EtherType type,
    const void* payload,
    uint16_t payload_length,
    uint16_t* frame_length
) {
    if(output == nullptr || frame_length == nullptr || payload_length > kMaximumPayloadLength ||
       (payload_length != 0 && payload == nullptr)) {
        return false;
    }
    const uint16_t padded_payload_length =
        payload_length < kMinimumPayloadLength ? kMinimumPayloadLength : payload_length;
    const uint16_t total_length = kHeaderLength + padded_payload_length;
    if(capacity < total_length) {
        return false;
    }

    copy_bytes(output, destination.bytes, sizeof(destination.bytes));
    copy_bytes(output + sizeof(destination.bytes), source.bytes, sizeof(source.bytes));
    write_be16(output + 12, ether_type_value(type));
    if(payload_length != 0) {
        copy_bytes(output + kHeaderLength, static_cast<const uint8_t*>(payload), payload_length);
    }
    for(uint16_t index = payload_length; index < padded_payload_length; ++index) {
        output[kHeaderLength + index] = 0;
    }
    *frame_length = total_length;
    return true;
}

bool parse_frame(const void* data, uint16_t length, FrameView* output) {
    if(data == nullptr || output == nullptr || length < kHeaderLength || length > kMaximumFrameLength) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    copy_bytes(output->destination.bytes, bytes, sizeof(output->destination.bytes));
    copy_bytes(output->source.bytes, bytes + sizeof(output->destination.bytes), sizeof(output->source.bytes));
    output->type = static_cast<EtherType>(read_be16(bytes + 12));
    output->payload = bytes + kHeaderLength;
    output->payload_length = length - kHeaderLength;
    return true;
}

bool addresses_equal(MacAddress first, MacAddress second) {
    for(uint8_t index = 0; index < sizeof(first.bytes); ++index) {
        if(first.bytes[index] != second.bytes[index]) {
            return false;
        }
    }
    return true;
}

bool is_broadcast(MacAddress address) {
    for(const uint8_t byte : address.bytes) {
        if(byte != 0xff) {
            return false;
        }
    }
    return true;
}

bool is_multicast(MacAddress address) {
    return (address.bytes[0] & 1U) != 0;
}

bool is_for_us(const FrameView& frame, MacAddress local_address) {
    return addresses_equal(frame.destination, local_address) || is_broadcast(frame.destination) ||
           is_multicast(frame.destination);
}

} // namespace ethernet

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
