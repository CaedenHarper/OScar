#pragma once

#include "arp.hpp"

#include <stdint.h>

namespace dns {

constexpr uint16_t kHeaderLength = 12;
constexpr uint16_t kMaximumNameLength = 253;

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    BufferTooSmall,
    Malformed,
    NameNotFound,
    ServerFailure,
    NoAddress,
};

/** Build a recursive IPv4 A-record query for hostname into caller-owned storage. */
Status build_query(const char* hostname, uint16_t identifier, void* output, uint16_t capacity, uint16_t* length);

/** Parse a DNS response and return its first IPv4 A record. */
Status parse_response(const void* data, uint16_t length, uint16_t expected_identifier, arp::Ipv4Address* address);

} // namespace dns
