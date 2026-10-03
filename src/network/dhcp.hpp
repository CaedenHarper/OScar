#pragma once

#include "arp.hpp"
#include "ethernet.hpp"

#include <stdint.h>

namespace dhcp {

constexpr uint16_t kFixedHeaderLength = 236;
constexpr uint16_t kCookieLength = 4;
constexpr uint16_t kMinimumPacketLength = kFixedHeaderLength + kCookieLength;

enum class MessageType : uint8_t {
    Discover = 1,
    Offer = 2,
    Request = 3,
    Ack = 5,
};

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
    Malformed,
    WrongTransaction,
    WrongMessageType,
};

struct Configuration {
    arp::Ipv4Address address;
    arp::Ipv4Address netmask;
    arp::Ipv4Address gateway;
    arp::Ipv4Address dns_server;
    arp::Ipv4Address server_address;
    uint32_t lease_seconds;
};

/** Build a DHCP discover message for the supplied client hardware address. */
Status build_discover(
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Build a DHCP request selecting an offered address and DHCP server. */
Status build_request(
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    arp::Ipv4Address requested_address,
    arp::Ipv4Address server_address,
    void* output,
    uint16_t capacity,
    uint16_t* length
);

/** Parse a DHCP offer or acknowledgement and extract its network configuration. */
Status parse_response(
    const void* data,
    uint16_t length,
    ethernet::MacAddress client_hardware,
    uint32_t transaction_id,
    MessageType expected_type,
    Configuration* configuration
);

} // namespace dhcp
