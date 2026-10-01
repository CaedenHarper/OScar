#pragma once

#include "arp.hpp"

#include <stdint.h>

namespace network {

enum class PingStatus : uint8_t {
    Success,
    NotInitialized,
    InvalidArgument,
    AddressUnreachable,
    Timeout,
    IoError,
};

/** Initialize the static QEMU-compatible IPv4 network interface. */
bool initialize();

/** Return whether the network device and protocol interface are initialized. */
bool is_initialized();

/** Send an ICMP echo request and return its elapsed timer ticks on success. */
PingStatus ping(arp::Ipv4Address destination, uint64_t timeout_ticks, uint16_t identifier, uint64_t* elapsed_ticks);

} // namespace network
