#include "dhcp.hpp"

#include <stdint.h>

// DHCP is a variable-length, untrusted wire format. Keep every option cursor
// bounded so a malformed lease cannot make boot read past the received frame.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace dhcp {

namespace {

constexpr uint8_t kBootReply = 2;
constexpr uint8_t kEthernetHardwareType = 1;
constexpr uint8_t kClientHardwareLength = 6;
constexpr uint16_t kBroadcastFlags = 0x8000;
constexpr uint32_t kMagicCookie = 0x63825363;
constexpr uint8_t kOptionPad = 0;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionRequestedIp = 50;
constexpr uint8_t kOptionServerIdentifier = 54;
constexpr uint8_t kOptionSubnetMask = 1;
constexpr uint8_t kOptionRouter = 3;
constexpr uint8_t kOptionDns = 6;
constexpr uint8_t kOptionLeaseTime = 51;
constexpr uint16_t kTransactionOffset = 4;
constexpr uint16_t kFlagsOffset = 10;
constexpr uint16_t kYourAddressOffset = 16;
constexpr uint16_t kClientHardwareOffset = 28;
constexpr uint16_t kCookieOffset = kFixedHeaderLength;
constexpr uint16_t kOptionsOffset = kCookieOffset + kCookieLength;

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

bool address_is_zero(arp::Ipv4Address address) {
    return address.bytes[0] == 0 && address.bytes[1] == 0 && address.bytes[2] == 0 && address.bytes[3] == 0;
}

bool append_option(
    uint8_t* output,
    uint16_t capacity,
    uint16_t* offset,
    uint8_t code,
    const void* value,
    uint8_t length
) {
    if(*offset > capacity - 2 || static_cast<uint16_t>(*offset + 2 + length) > capacity || value == nullptr) {
        return false;
    }
    output[(*offset)++] = code;
    output[(*offset)++] = length;
    const auto* bytes = static_cast<const uint8_t*>(value);
    for(uint8_t index = 0; index < length; ++index) {
        output[(*offset)++] = bytes[index];
    }
    return true;
}

Status initialize_message(
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    MessageType type,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    arp::Ipv4Address* requested_address,
    arp::Ipv4Address* server_address
) {
    if(output == nullptr || length == nullptr || capacity < kMinimumPacketLength) {
        return output == nullptr || length == nullptr ? Status::InvalidArgument : Status::BufferTooSmall;
    }
    auto* bytes = static_cast<uint8_t*>(output);
    for(uint16_t index = 0; index < kMinimumPacketLength; ++index) {
        bytes[index] = 0;
    }
    bytes[0] = 1;
    bytes[1] = kEthernetHardwareType;
    bytes[2] = kClientHardwareLength;
    write_be32(bytes + kTransactionOffset, transaction_id);
    write_be16(bytes + kFlagsOffset, kBroadcastFlags);
    for(uint8_t index = 0; index < ethernet::kMacAddressLength; ++index) {
        bytes[kClientHardwareOffset + index] = client_hardware.bytes[index];
    }
    write_be32(bytes + kCookieOffset, kMagicCookie);
    uint16_t offset = kOptionsOffset;
    const uint8_t message = static_cast<uint8_t>(type);
    if(!append_option(bytes, capacity, &offset, kOptionMessageType, &message, 1)) {
        return Status::BufferTooSmall;
    }
    if(requested_address != nullptr && server_address != nullptr &&
       (!append_option(
            bytes, capacity, &offset, kOptionRequestedIp, requested_address->bytes, sizeof(requested_address->bytes)
        ) ||
        !append_option(
            bytes, capacity, &offset, kOptionServerIdentifier, server_address->bytes, sizeof(server_address->bytes)
        ))) {
        return Status::BufferTooSmall;
    }
    static constexpr uint8_t kParameterRequestList[] = {kOptionSubnetMask, kOptionRouter, kOptionDns, kOptionLeaseTime};
    if(!append_option(bytes, capacity, &offset, 55, kParameterRequestList, sizeof(kParameterRequestList)) ||
       offset >= capacity) {
        return Status::BufferTooSmall;
    }
    bytes[offset++] = kOptionEnd;
    *length = offset;
    return Status::Success;
}

} // namespace

Status build_discover(
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    void* output,
    uint16_t capacity,
    uint16_t* length
) {
    return initialize_message(
        client_hardware, transaction_id, MessageType::Discover, output, capacity, length, nullptr, nullptr
    );
}

Status build_request(
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    arp::Ipv4Address requested_address,
    arp::Ipv4Address server_address,
    void* output,
    uint16_t capacity,
    uint16_t* length
) {
    if(address_is_zero(requested_address) || address_is_zero(server_address)) {
        return Status::InvalidArgument;
    }
    return initialize_message(
        client_hardware,
        transaction_id,
        MessageType::Request,
        output,
        capacity,
        length,
        &requested_address,
        &server_address
    );
}

Status parse_response(
    const void* data,
    uint16_t length,
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    MessageType expected_type,
    Configuration* configuration
) {
    if(data == nullptr || configuration == nullptr || length < kOptionsOffset) {
        return Status::InvalidArgument;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    if(bytes[0] != kBootReply || bytes[1] != kEthernetHardwareType || bytes[2] != kClientHardwareLength ||
       read_be32(bytes + kTransactionOffset) != transaction_id || read_be32(bytes + kCookieOffset) != kMagicCookie) {
        return Status::WrongTransaction;
    }
    for(uint8_t index = 0; index < ethernet::kMacAddressLength; ++index) {
        if(bytes[kClientHardwareOffset + index] != client_hardware.bytes[index]) {
            return Status::WrongTransaction;
        }
    }
    configuration->address = {
        {bytes[kYourAddressOffset],
         bytes[kYourAddressOffset + 1],
         bytes[kYourAddressOffset + 2],
         bytes[kYourAddressOffset + 3]}
    };
    configuration->netmask = {};
    configuration->gateway = {};
    configuration->dns_server = {};
    configuration->server_address = {};
    configuration->lease_seconds = 0;
    uint16_t offset = kOptionsOffset;
    bool message_seen = false;
    while(offset < length) {
        const uint8_t code = bytes[offset++];
        if(code == kOptionPad) {
            continue;
        }
        if(code == kOptionEnd) {
            break;
        }
        if(offset >= length) {
            return Status::Malformed;
        }
        const uint8_t option_length = bytes[offset++];
        if(option_length > length - offset) {
            return Status::Malformed;
        }
        const uint8_t* value = bytes + offset;
        if(code == kOptionMessageType && option_length == 1) {
            message_seen = true;
            if(value[0] != static_cast<uint8_t>(expected_type)) {
                return Status::WrongMessageType;
            }
        } else if(code == kOptionSubnetMask && option_length == 4) {
            for(uint8_t index = 0; index < 4; ++index)
                configuration->netmask.bytes[index] = value[index];
        } else if(code == kOptionRouter && option_length >= 4) {
            for(uint8_t index = 0; index < 4; ++index)
                configuration->gateway.bytes[index] = value[index];
        } else if(code == kOptionDns && option_length >= 4) {
            for(uint8_t index = 0; index < 4; ++index)
                configuration->dns_server.bytes[index] = value[index];
        } else if(code == kOptionLeaseTime && option_length == 4) {
            configuration->lease_seconds = read_be32(value);
        } else if(code == kOptionServerIdentifier && option_length == 4) {
            for(uint8_t index = 0; index < 4; ++index)
                configuration->server_address.bytes[index] = value[index];
        }
        offset = static_cast<uint16_t>(offset + option_length);
    }
    if(!message_seen || address_is_zero(configuration->address) || address_is_zero(configuration->netmask) ||
       address_is_zero(configuration->gateway) || address_is_zero(configuration->dns_server) ||
       address_is_zero(configuration->server_address)) {
        return Status::Malformed;
    }
    return Status::Success;
}

} // namespace dhcp

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
