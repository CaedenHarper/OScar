#include "arp.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "tcp.hpp"
#include "tcp_connection.hpp"

#include <stdint.h>

// The fake transport records complete state-machine emissions so this test can
// exercise retransmission and teardown without depending on a live NIC.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//             hicpp-member-init, hicpp-signed-bitwise)

namespace self_tests_detail {

namespace {

constexpr arp::Ipv4Address kLocalIp = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kRemoteIp = {{192, 0, 2, 1}};
constexpr uint16_t kLocalPort = 49152;
constexpr uint16_t kRemotePort = 80;
constexpr uint32_t kInitialSequence = 100;
constexpr uint32_t kRemoteSequence = 500;

struct SentSegment {
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t flags;
    uint16_t length;
    uint8_t payload[tcp_connection::kReceiveBufferSize];
};

struct FakeTransport {
    SentSegment last;
    uint16_t count;
};

bool send_fake(
    void* context,
    const tcp_connection::Connection&,
    uint32_t sequence,
    uint32_t acknowledgment,
    uint8_t flags,
    const void* payload,
    uint16_t payload_length
) {
    auto* transport = static_cast<FakeTransport*>(context);
    transport->last.sequence = sequence;
    transport->last.acknowledgment = acknowledgment;
    transport->last.flags = flags;
    transport->last.length = payload_length;
    const auto* bytes = static_cast<const uint8_t*>(payload);
    for(uint16_t index = 0; index < payload_length; ++index) {
        transport->last.payload[index] = bytes[index];
    }
    ++transport->count;
    return true;
}

tcp::SegmentView segment(
    uint32_t sequence,
    uint32_t acknowledgment,
    uint8_t flags,
    const void* payload,
    uint16_t length
) {
    return {
        kRemotePort,
        kLocalPort,
        sequence,
        acknowledgment,
        flags,
        4096,
        0,
        static_cast<const uint8_t*>(payload),
        length,
    };
}

} // namespace

void test_tcp_connection() {
    tcp_connection::Connection connection;
    tcp_connection::initialize(&connection, kLocalIp, kRemoteIp, kLocalPort, kRemotePort, kInitialSequence);
    FakeTransport transport;
    transport.count = 0;

    if(tcp_connection::open(&connection, send_fake, &transport, 0) != tcp_connection::Result::Success ||
       connection.state != tcp_connection::State::SynSent || transport.last.sequence != kInitialSequence ||
       transport.last.flags != tcp::kSyn) {
        panic::halt("TCP connection smoke test could not send SYN");
    }

    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(kRemoteSequence, kInitialSequence + 1, tcp::kSyn | tcp::kAck, nullptr, 0),
           1
       ) != tcp_connection::Result::Success ||
       connection.state != tcp_connection::State::Established || transport.last.flags != tcp::kAck ||
       transport.last.acknowledgment != kRemoteSequence + 1) {
        panic::halt("TCP connection smoke test could not complete handshake");
    }

    static const uint8_t request[] = {'G', 'E', 'T'};
    if(tcp_connection::send(&connection, send_fake, &transport, request, sizeof(request), 2) !=
           tcp_connection::Result::Success ||
       transport.last.sequence != kInitialSequence + 1 || transport.last.length != sizeof(request) ||
       transport.last.payload[0] != request[0]) {
        panic::halt("TCP connection smoke test could not send data");
    }
    if(tcp_connection::send(&connection, send_fake, &transport, request, sizeof(request), 2) !=
       tcp_connection::Result::WouldBlock) {
        panic::halt("TCP connection smoke test allowed multiple outstanding segments");
    }

    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(kRemoteSequence + 1, kInitialSequence + 4, tcp::kAck, nullptr, 0),
           3
       ) != tcp_connection::Result::Success) {
        panic::halt("TCP connection smoke test did not accept data acknowledgment");
    }

    static const uint8_t response[] = {'O', 'K'};
    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(kRemoteSequence + 1, kInitialSequence + 4, tcp::kAck, response, sizeof(response)),
           4
       ) != tcp_connection::Result::Success ||
       transport.last.acknowledgment != kRemoteSequence + 3) {
        panic::halt("TCP connection smoke test did not accept in-order data");
    }
    uint8_t received[sizeof(response)] = {};
    uint16_t received_length = 0;
    if(tcp_connection::receive(&connection, received, sizeof(received), &received_length) !=
           tcp_connection::Result::Success ||
       received_length != sizeof(response) || received[0] != response[0]) {
        panic::halt("TCP connection smoke test did not buffer received data");
    }

    static uint8_t first_queued_chunk[3000];
    static uint8_t second_queued_chunk[2000];
    for(uint16_t index = 0; index < sizeof(first_queued_chunk); ++index) {
        first_queued_chunk[index] = static_cast<uint8_t>(index);
    }
    for(uint16_t index = 0; index < sizeof(second_queued_chunk); ++index) {
        second_queued_chunk[index] = static_cast<uint8_t>(index + 31);
    }
    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(
               kRemoteSequence + 3, kInitialSequence + 4, tcp::kAck, first_queued_chunk, sizeof(first_queued_chunk)
           ),
           4
       ) != tcp_connection::Result::Success) {
        panic::halt("TCP connection smoke test could not queue the first receive chunk");
    }
    uint8_t first_read[2500];
    if(tcp_connection::receive(&connection, first_read, sizeof(first_read), &received_length) !=
           tcp_connection::Result::Success ||
       received_length != sizeof(first_read)) {
        panic::halt("TCP connection smoke test could not partially consume queued data");
    }
    for(uint16_t index = 0; index < sizeof(first_read); ++index) {
        if(first_read[index] != first_queued_chunk[index]) {
            panic::halt("TCP connection smoke test returned corrupted queued data");
        }
    }
    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(
               kRemoteSequence + 3003, kInitialSequence + 4, tcp::kAck, second_queued_chunk, sizeof(second_queued_chunk)
           ),
           4
       ) != tcp_connection::Result::Success) {
        panic::halt("TCP connection smoke test could not wrap the receive queue");
    }
    uint8_t second_read[2500];
    if(tcp_connection::receive(&connection, second_read, sizeof(second_read), &received_length) !=
           tcp_connection::Result::Success ||
       received_length != sizeof(second_read)) {
        panic::halt("TCP connection smoke test could not drain wrapped receive data");
    }
    for(uint16_t index = 0; index < 500; ++index) {
        if(second_read[index] != first_queued_chunk[index + 2500]) {
            panic::halt("TCP connection smoke test lost queued data at the wrap boundary");
        }
    }
    for(uint16_t index = 500; index < sizeof(second_read); ++index) {
        if(second_read[index] != second_queued_chunk[index - 500]) {
            panic::halt("TCP connection smoke test reordered wrapped receive data");
        }
    }

    if(tcp_connection::send(&connection, send_fake, &transport, request, sizeof(request), 5) !=
       tcp_connection::Result::Success) {
        panic::halt("TCP connection smoke test could not create retransmission");
    }
    const uint16_t sends_before_poll = transport.count;
    if(tcp_connection::poll(&connection, send_fake, &transport, 5 + tcp_connection::kRetransmissionTimeoutTicks) !=
           tcp_connection::Result::Success ||
       transport.count != sends_before_poll + 1) {
        panic::halt("TCP connection smoke test did not retransmit expired data");
    }

    // The retransmitted data is acknowledged before initiating FIN.
    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(kRemoteSequence + 5003, kInitialSequence + 7, tcp::kAck, nullptr, 0),
           7
       ) != tcp_connection::Result::Success ||
       tcp_connection::close(&connection, send_fake, &transport, 8) != tcp_connection::Result::Success ||
       connection.state != tcp_connection::State::FinWait || (transport.last.flags & tcp::kFin) == 0) {
        panic::halt("TCP connection smoke test could not begin graceful close");
    }

    if(tcp_connection::process(
           &connection,
           send_fake,
           &transport,
           segment(kRemoteSequence + 5003, kInitialSequence + 8, tcp::kFin | tcp::kAck, nullptr, 0),
           9
       ) != tcp_connection::Result::Success ||
       connection.state != tcp_connection::State::TimeWait ||
       tcp_connection::poll(&connection, send_fake, &transport, 9 + tcp_connection::kRetransmissionTimeoutTicks) !=
           tcp_connection::Result::Closed ||
       connection.state != tcp_connection::State::Closed) {
        panic::halt("TCP connection smoke test could not complete graceful close");
    }

    tcp_connection::initialize(&connection, kLocalIp, kRemoteIp, kLocalPort, kRemotePort, kInitialSequence);
    if(tcp_connection::process(
           &connection, send_fake, &transport, segment(kRemoteSequence, 0, tcp::kRst, nullptr, 0), 0
       ) != tcp_connection::Result::Reset ||
       connection.state != tcp_connection::State::Reset) {
        panic::halt("TCP connection smoke test did not handle reset");
    }
    serial::write("TCP connection state-machine smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//           hicpp-member-init, hicpp-signed-bitwise)
