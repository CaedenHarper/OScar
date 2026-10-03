#include "tcp_connection.hpp"

#include <stdint.h>

// The connection object deliberately permits only one outstanding segment.
// This keeps retransmission and acknowledgment rules explicit until a later
// sliding-window implementation is needed by larger HTTP responses.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace tcp_connection {

namespace {

bool sequence_before(uint32_t first, uint32_t second) {
    return static_cast<int32_t>(first - second) < 0;
}

bool sequence_after(uint32_t first, uint32_t second) {
    return static_cast<int32_t>(first - second) > 0;
}

bool send_segment(
    Connection* connection,
    SendCallback send,
    void* context,
    uint32_t sequence,
    uint8_t flags,
    const void* payload,
    uint16_t length,
    uint64_t now,
    bool track
) {
    if(!send(context, *connection, sequence, connection->receive_next, flags, payload, length)) {
        return false;
    }
    if(track) {
        connection->outstanding_flags = flags;
        connection->outstanding_sequence = sequence;
        connection->outstanding_length = length;
        if(length != 0) {
            const auto* bytes = static_cast<const uint8_t*>(payload);
            for(uint16_t index = 0; index < length; ++index) {
                connection->outstanding_payload[index] = bytes[index];
            }
        }
        connection->retransmission_deadline = now + kRetransmissionTimeoutTicks;
        connection->retransmissions = 0;
    }
    return true;
}

void clear_outstanding(Connection* connection) {
    connection->outstanding_flags = 0;
    connection->outstanding_length = 0;
    connection->retransmission_deadline = 0;
    connection->retransmissions = 0;
}

bool outstanding(const Connection& connection) {
    return connection.outstanding_flags != 0;
}

Result accept_ack(Connection* connection, uint32_t acknowledgment) {
    if(!outstanding(*connection) || sequence_before(acknowledgment, connection->send_unacknowledged) ||
       sequence_after(acknowledgment, connection->send_next)) {
        return Result::Ignored;
    }
    if(acknowledgment == connection->send_next) {
        clear_outstanding(connection);
    }
    connection->send_unacknowledged = acknowledgment;
    return Result::Success;
}

} // namespace

void initialize(
    Connection* connection,
    arp::Ipv4Address local_address,
    arp::Ipv4Address remote_address,
    uint16_t local_port,
    uint16_t remote_port,
    uint32_t initial_sequence
) {
    if(connection == nullptr) {
        return;
    }
    auto* bytes = reinterpret_cast<uint8_t*>(connection);
    for(uint64_t index = 0; index < sizeof(*connection); ++index) {
        bytes[index] = 0;
    }
    connection->local_address = local_address;
    connection->remote_address = remote_address;
    connection->local_port = local_port;
    connection->remote_port = remote_port;
    connection->state = State::Closed;
    connection->send_unacknowledged = initial_sequence;
    connection->send_next = initial_sequence;
}

Result open(Connection* connection, SendCallback callback, void* context, uint64_t now) {
    if(connection == nullptr || callback == nullptr || connection->state != State::Closed) {
        return Result::InvalidArgument;
    }
    if(!send_segment(connection, callback, context, connection->send_next, tcp::kSyn, nullptr, 0, now, true)) {
        return Result::IoError;
    }
    ++connection->send_next;
    connection->state = State::SynSent;
    return Result::Success;
}

Result send(
    Connection* connection,
    SendCallback callback,
    void* context,
    const void* data,
    uint16_t length,
    uint64_t now
) {
    if(connection == nullptr || callback == nullptr || (length != 0 && data == nullptr)) {
        return Result::InvalidArgument;
    }
    if(connection->state != State::Established) {
        return connection->state == State::Reset ? Result::Reset : Result::Closed;
    }
    if(length == 0 || length > kReceiveBufferSize || outstanding(*connection)) {
        return length == 0 || length > kReceiveBufferSize ? Result::InvalidArgument : Result::WouldBlock;
    }
    if(!send_segment(
           connection, callback, context, connection->send_next, tcp::kAck | tcp::kPsh, data, length, now, true
       )) {
        return Result::IoError;
    }
    connection->send_next += length;
    return Result::Success;
}

Result process(
    Connection* connection,
    SendCallback callback,
    void* context,
    const tcp::SegmentView& segment,
    uint64_t now
) {
    if(connection == nullptr || callback == nullptr) {
        return Result::InvalidArgument;
    }
    if((segment.flags & tcp::kRst) != 0) {
        connection->state = State::Reset;
        clear_outstanding(connection);
        return Result::Reset;
    }

    if(connection->state == State::SynSent) {
        if((segment.flags & (tcp::kSyn | tcp::kAck)) != (tcp::kSyn | tcp::kAck) ||
           segment.acknowledgment != connection->send_next) {
            return Result::Ignored;
        }
        connection->receive_next = segment.sequence + 1;
        connection->send_unacknowledged = segment.acknowledgment;
        clear_outstanding(connection);
        if(!send_segment(connection, callback, context, connection->send_next, tcp::kAck, nullptr, 0, now, false)) {
            return Result::IoError;
        }
        connection->state = State::Established;
        return Result::Success;
    }

    if(connection->state == State::TimeWait) {
        if((segment.flags & tcp::kFin) != 0 && segment.sequence == connection->receive_next) {
            ++connection->receive_next;
            if(!send_segment(connection, callback, context, connection->send_next, tcp::kAck, nullptr, 0, now, false)) {
                return Result::IoError;
            }
        }
        return Result::Ignored;
    }

    if(connection->state != State::Established && connection->state != State::FinWait) {
        return Result::Closed;
    }

    if((segment.flags & tcp::kAck) != 0) {
        const Result acknowledgment_result = accept_ack(connection, segment.acknowledgment);
        if(acknowledgment_result == Result::Ignored && connection->state == State::FinWait) {
            return Result::Ignored;
        }
    }

    if(segment.payload_length != 0) {
        if(segment.sequence != connection->receive_next) {
            if(!send_segment(connection, callback, context, connection->send_next, tcp::kAck, nullptr, 0, now, false)) {
                return Result::IoError;
            }
            return Result::Ignored;
        }
        if(segment.payload_length > kReceiveBufferSize - connection->receive_length) {
            return Result::BufferFull;
        }
        for(uint16_t index = 0; index < segment.payload_length; ++index) {
            connection->receive_buffer[(connection->receive_write_position + index) % kReceiveBufferSize] =
                segment.payload[index];
        }
        connection->receive_write_position =
            static_cast<uint16_t>((connection->receive_write_position + segment.payload_length) % kReceiveBufferSize);
        connection->receive_length += segment.payload_length;
        connection->receive_next += segment.payload_length;
    }

    if((segment.flags & tcp::kFin) != 0) {
        if(segment.sequence + segment.payload_length != connection->receive_next) {
            return Result::Ignored;
        }
        ++connection->receive_next;
        connection->peer_closed = true;
        connection->state = State::TimeWait;
        connection->time_wait_deadline = now + kRetransmissionTimeoutTicks;
    }
    if(segment.payload_length != 0 || (segment.flags & tcp::kFin) != 0) {
        if(!send_segment(connection, callback, context, connection->send_next, tcp::kAck, nullptr, 0, now, false)) {
            return Result::IoError;
        }
    }
    if(connection->state == State::FinWait && !outstanding(*connection) && connection->peer_closed) {
        connection->state = State::TimeWait;
        connection->time_wait_deadline = now + kRetransmissionTimeoutTicks;
    }
    return segment.payload_length == 0 ? Result::Success : Result::Success;
}

Result receive(Connection* connection, void* output, uint16_t capacity, uint16_t* length) {
    if(connection == nullptr || output == nullptr || length == nullptr) {
        return Result::InvalidArgument;
    }
    const uint16_t copied = connection->receive_length < capacity ? connection->receive_length : capacity;
    auto* destination = static_cast<uint8_t*>(output);
    for(uint16_t index = 0; index < copied; ++index) {
        destination[index] =
            connection->receive_buffer[(connection->receive_read_position + index) % kReceiveBufferSize];
    }
    connection->receive_read_position =
        static_cast<uint16_t>((connection->receive_read_position + copied) % kReceiveBufferSize);
    connection->receive_length -= copied;
    *length = copied;
    if(copied != 0) {
        return Result::Success;
    }
    return connection->state == State::Reset ? Result::Reset
           : connection->peer_closed         ? Result::Closed
                                             : Result::WouldBlock;
}

Result close(Connection* connection, SendCallback callback, void* context, uint64_t now) {
    if(connection == nullptr || callback == nullptr) {
        return Result::InvalidArgument;
    }
    if(connection->state != State::Established || outstanding(*connection)) {
        return connection->state == State::Reset ? Result::Reset : Result::WouldBlock;
    }
    if(!send_segment(
           connection, callback, context, connection->send_next, tcp::kFin | tcp::kAck, nullptr, 0, now, true
       )) {
        return Result::IoError;
    }
    ++connection->send_next;
    connection->state = State::FinWait;
    return Result::Success;
}

Result poll(Connection* connection, SendCallback callback, void* context, uint64_t now) {
    if(connection == nullptr || callback == nullptr) {
        return Result::InvalidArgument;
    }
    if(connection->state == State::TimeWait) {
        if(now >= connection->time_wait_deadline) {
            connection->state = State::Closed;
            return Result::Closed;
        }
        return Result::Success;
    }
    if(!outstanding(*connection) || now < connection->retransmission_deadline) {
        return Result::Success;
    }
    if(connection->retransmissions >= kMaximumRetransmissions) {
        connection->state = State::Reset;
        clear_outstanding(connection);
        return Result::Timeout;
    }
    if(!send_segment(
           connection,
           callback,
           context,
           connection->outstanding_sequence,
           connection->outstanding_flags,
           connection->outstanding_length == 0 ? nullptr : connection->outstanding_payload,
           connection->outstanding_length,
           now,
           true
       )) {
        return Result::IoError;
    }
    ++connection->retransmissions;
    return Result::Success;
}

} // namespace tcp_connection

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
