#include "dns.hpp"

#include <stdint.h>

// DNS packets are untrusted network input. The bounds checks in this file are
// deliberately kept local to each cursor operation so malformed compression
// pointers cannot turn a packet into an out-of-bounds read or an infinite loop.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace dns {

namespace {

constexpr uint16_t kQuestionTypeA = 1;
constexpr uint16_t kClassInternet = 1;
constexpr uint16_t kResponseFlag = 0x8000;
constexpr uint16_t kTruncatedFlag = 0x0200;
constexpr uint16_t kResponseCodeMask = 0x000f;
constexpr uint8_t kMaximumLabels = 127;

uint16_t read_be16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1]);
}

void write_be16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

uint32_t read_be32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) | (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) | bytes[3];
}

bool skip_name(const uint8_t* packet, uint16_t length, uint16_t* offset) {
    if(packet == nullptr || offset == nullptr || *offset >= length) {
        return false;
    }
    const uint16_t name_start = *offset;
    uint16_t cursor = *offset;
    uint8_t labels = 0;
    for(;;) {
        if(cursor >= length || labels++ >= kMaximumLabels) {
            return false;
        }
        const uint8_t label = packet[cursor++];
        if(label == 0) {
            *offset = cursor;
            return true;
        }
        if((label & 0xc0U) == 0xc0U) {
            if(cursor >= length) {
                return false;
            }
            const uint16_t pointer = static_cast<uint16_t>(((label & 0x3fU) << 8U) | packet[cursor]);
            // Compression pointers must point backward; rejecting forward or
            // self-referential pointers bounds traversal without a recursion limit.
            if(pointer >= name_start) {
                return false;
            }
            *offset = static_cast<uint16_t>(cursor + 1);
            return *offset <= length;
        }
        if((label & 0xc0U) != 0 || label > 63 || label > length - cursor) {
            return false;
        }
        cursor = static_cast<uint16_t>(cursor + label);
    }
}

bool read_question(const uint8_t* packet, uint16_t length, uint16_t* offset) {
    if(length < 4 || !skip_name(packet, length, offset) || *offset > length - 4) {
        return false;
    }
    *offset = static_cast<uint16_t>(*offset + 4);
    return true;
}

bool read_record_header(
    const uint8_t* packet,
    uint16_t length,
    uint16_t* offset,
    uint16_t* type,
    uint16_t* class_code,
    uint32_t* ttl,
    uint16_t* data_length
) {
    if(length < 10 || !skip_name(packet, length, offset) || *offset > length - 10) {
        return false;
    }
    *type = read_be16(packet + *offset);
    *class_code = read_be16(packet + *offset + 2);
    *ttl = read_be32(packet + *offset + 4);
    *data_length = read_be16(packet + *offset + 8);
    *offset = static_cast<uint16_t>(*offset + 10);
    return *data_length <= length - *offset;
}

} // namespace

Status build_query(const char* hostname, uint16_t identifier, void* output, uint16_t capacity, uint16_t* length) {
    if(hostname == nullptr || output == nullptr || length == nullptr || hostname[0] == '\0') {
        return Status::InvalidArgument;
    }
    auto* bytes = static_cast<uint8_t*>(output);
    if(capacity < kHeaderLength) {
        return Status::BufferTooSmall;
    }
    for(uint16_t index = 0; index < kHeaderLength; ++index) {
        bytes[index] = 0;
    }
    write_be16(bytes, identifier);
    write_be16(bytes + 2, 0x0100);
    write_be16(bytes + 4, 1);

    uint16_t output_offset = kHeaderLength;
    uint16_t label_start = output_offset++;
    uint16_t label_length = 0;
    uint16_t name_length = 0;
    for(uint16_t index = 0;; ++index) {
        const char character = hostname[index];
        if(character == '.' || character == '\0') {
            if(label_length == 0 || label_length > 63 || output_offset >= capacity) {
                return Status::InvalidArgument;
            }
            bytes[label_start] = static_cast<uint8_t>(label_length);
            if(name_length > kMaximumNameLength - static_cast<uint16_t>(label_length + 1)) {
                return Status::InvalidArgument;
            }
            name_length = static_cast<uint16_t>(name_length + label_length + 1);
            if(character == '\0') {
                if(output_offset >= capacity - 5) {
                    return Status::BufferTooSmall;
                }
                bytes[output_offset++] = 0;
                write_be16(bytes + output_offset, kQuestionTypeA);
                output_offset = static_cast<uint16_t>(output_offset + 2);
                write_be16(bytes + output_offset, kClassInternet);
                output_offset = static_cast<uint16_t>(output_offset + 2);
                *length = output_offset;
                return Status::Success;
            }
            label_start = output_offset;
            ++output_offset;
            label_length = 0;
        } else {
            if((character < 'a' || character > 'z') && (character < 'A' || character > 'Z') &&
               (character < '0' || character > '9') && character != '-') {
                return Status::InvalidArgument;
            }
            if(output_offset >= capacity || label_length == 63) {
                return Status::BufferTooSmall;
            }
            bytes[output_offset++] = static_cast<uint8_t>(character);
            ++label_length;
        }
    }
}

Status parse_response(const void* data, uint16_t length, uint16_t expected_identifier, arp::Ipv4Address* address) {
    if(data == nullptr || address == nullptr || length < kHeaderLength) {
        return Status::InvalidArgument;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if(read_be16(bytes) != expected_identifier) {
        return Status::Malformed;
    }
    const uint16_t flags = read_be16(bytes + 2);
    if((flags & kResponseFlag) == 0 || (flags & kTruncatedFlag) != 0) {
        return Status::Malformed;
    }
    const uint16_t response_code = flags & kResponseCodeMask;
    if(response_code == 3) {
        return Status::NameNotFound;
    }
    if(response_code != 0) {
        return Status::ServerFailure;
    }
    const uint16_t questions = read_be16(bytes + 4);
    const uint16_t answers = read_be16(bytes + 6);
    if(questions == 0) {
        return Status::Malformed;
    }
    uint16_t offset = kHeaderLength;
    for(uint16_t index = 0; index < questions; ++index) {
        if(!read_question(bytes, length, &offset)) {
            return Status::Malformed;
        }
    }
    for(uint16_t index = 0; index < answers; ++index) {
        uint16_t type = 0;
        uint16_t class_code = 0;
        uint32_t ttl = 0;
        uint16_t data_length = 0;
        if(!read_record_header(bytes, length, &offset, &type, &class_code, &ttl, &data_length)) {
            return Status::Malformed;
        }
        (void)ttl;
        if(type == kQuestionTypeA && class_code == kClassInternet && data_length == 4) {
            address->bytes[0] = bytes[offset];
            address->bytes[1] = bytes[offset + 1];
            address->bytes[2] = bytes[offset + 2];
            address->bytes[3] = bytes[offset + 3];
            return Status::Success;
        }
        // Unknown records are valid and are skipped without interpreting their
        // RDATA as an address. This lets CNAME and IPv6 records coexist with A.
        offset = static_cast<uint16_t>(offset + data_length);
    }
    return Status::NoAddress;
}

} // namespace dns

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
