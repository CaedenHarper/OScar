#pragma once

#include "arp.hpp"
#include "spinlock.hpp"
#include "tcp.hpp"

#include <stdint.h>

namespace tcp_connection {

constexpr uint16_t kReceiveBufferSize = 4096;
constexpr uint64_t kRetransmissionTimeoutTicks = 10;
constexpr uint8_t kMaximumRetransmissions = 3;

enum class State : uint8_t {
    Closed,
    SynSent,
    Established,
    FinWait,
    TimeWait,
    Reset,
};

enum class Result : uint8_t {
    Success,
    InvalidArgument,
    WouldBlock,
    BufferFull,
    Ignored,
    Reset,
    Closed,
    Timeout,
    IoError,
};

struct Connection;

/** Emit one TCP segment for a connection. */
using SendCallback = bool (*)(
    void* context,
    const Connection& connection,
    uint32_t sequence,
    uint32_t acknowledgment,
    uint8_t flags,
    const void* payload,
    uint16_t payload_length
);

struct Connection {
    arp::Ipv4Address local_address;
    arp::Ipv4Address remote_address;
    uint16_t local_port;
    uint16_t remote_port;
    State state;
    uint32_t send_unacknowledged;
    uint32_t send_next;
    uint32_t receive_next;
    uint64_t retransmission_deadline;
    uint8_t retransmissions;
    uint8_t outstanding_flags;
    uint32_t outstanding_sequence;
    uint16_t outstanding_length;
    uint8_t outstanding_payload[kReceiveBufferSize];
    uint8_t receive_buffer[kReceiveBufferSize];
    uint16_t receive_read_position;
    uint16_t receive_write_position;
    uint16_t receive_length;
    uint64_t time_wait_deadline;
    bool peer_closed;
    synchronization::Spinlock state_lock;
};

/** Initialize an inactive connection with a caller-selected initial sequence. */
void initialize(
    Connection* connection,
    arp::Ipv4Address local_address,
    arp::Ipv4Address remote_address,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence
);

/** Send a SYN and enter the SYN-SENT state. */
Result open(Connection* connection, SendCallback callback, void* context, uint64_t now);

/** Queue one in-order application segment for transmission. */
Result send(
    Connection* connection,
    SendCallback callback,
    void* context,
    const void* data,
    uint16_t length,
    uint64_t now
);

/** Process one validated TCP segment belonging to this connection. */
Result process(
    Connection* connection,
    SendCallback callback,
    void* context,
    const tcp::SegmentView& segment,
    uint64_t now
);

/** Copy received in-order bytes into a caller-owned buffer. */
Result receive(Connection* connection, void* output, uint16_t capacity, uint16_t* length);

/** Send FIN and begin graceful connection shutdown. */
Result close(Connection* connection, SendCallback callback, void* context, uint64_t now);

/** Retransmit an expired outstanding segment or expire TIME-WAIT. */
Result poll(Connection* connection, SendCallback callback, void* context, uint64_t now);

} // namespace tcp_connection
