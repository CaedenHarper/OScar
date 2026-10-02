#pragma once

#include "arp.hpp"
#include "tcp_connection.hpp"

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

enum class ResolveStatus : uint8_t {
    Success,
    NotInitialized,
    InvalidArgument,
    NameNotFound,
    Timeout,
    IoError,
};

enum class TcpStatus : uint8_t {
    Success,
    NotInitialized,
    InvalidArgument,
    NotConnected,
    AddressUnreachable,
    Timeout,
    Reset,
    IoError,
};

/** Initialize the static QEMU-compatible IPv4 network interface. */
bool initialize();

/** Return whether the network device and protocol interface are initialized. */
bool is_initialized();

/** Send an ICMP echo request and return its elapsed timer ticks on success. */
PingStatus ping(arp::Ipv4Address destination, uint64_t timeout_ticks, uint16_t identifier, uint64_t* elapsed_ticks);

/** Resolve a hostname through the configured DNS service and return its IPv4 address. */
ResolveStatus resolve_hostname(const char* hostname, uint64_t timeout_ticks, arp::Ipv4Address* address);

/** Establish an active IPv4 TCP connection using the polling network backend. */
TcpStatus tcp_connect(
    tcp_connection::Connection* connection,
    arp::Ipv4Address destination,
    uint16_t destination_port,
    uint64_t timeout_ticks
);

/** Send one bounded TCP segment and wait until it is acknowledged. */
TcpStatus tcp_send(tcp_connection::Connection* connection, const void* data, uint16_t length, uint64_t timeout_ticks);

/** Receive buffered or newly arrived TCP data, returning zero at peer EOF. */
TcpStatus tcp_receive(
    tcp_connection::Connection* connection,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t timeout_ticks
);

/** Gracefully close an active TCP connection. */
TcpStatus tcp_close(tcp_connection::Connection* connection, uint64_t timeout_ticks);

} // namespace network
