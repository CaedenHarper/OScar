#include "arp.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "tcp.hpp"

#include <stdint.h>

// The fixture intentionally edits raw TCP bytes to exercise checksum and
// header validation paths that are difficult to reach through a live socket.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//             hicpp-member-init, hicpp-signed-bitwise)

namespace self_tests_detail {

namespace {

constexpr arp::Ipv4Address kLocalIp = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kPeerIp = {{192, 0, 2, 1}};
constexpr arp::Ipv4Address kWrongPeerIp = {{192, 0, 2, 2}};
constexpr uint16_t kSourcePort = 40000;
constexpr uint16_t kDestinationPort = 80;
constexpr uint32_t kSequence = 0x12345678;
constexpr uint32_t kAcknowledgment = 0xabcdef01;
constexpr uint16_t kWindow = 4096;
constexpr uint16_t kUrgentPointer = 0;

} // namespace

void test_tcp() {
    static const uint8_t payload[] = {'G', 'E', 'T'};
    static uint8_t segment[128];
    uint16_t segment_length = 0;
    if(tcp::build_segment(
           kLocalIp,
           kPeerIp,
           kSourcePort,
           kDestinationPort,
           kSequence,
           kAcknowledgment,
           tcp::kAck | tcp::kPsh,
           kWindow,
           kUrgentPointer,
           payload,
           sizeof(payload),
           segment,
           sizeof(segment),
           &segment_length
       ) != tcp::Status::Success ||
       segment_length != tcp::kHeaderLength + sizeof(payload)) {
        panic::halt("TCP smoke test could not build a segment");
    }

    tcp::SegmentView parsed = {};
    if(!tcp::parse_segment(kLocalIp, kPeerIp, segment, segment_length, &parsed) || parsed.source_port != kSourcePort ||
       parsed.destination_port != kDestinationPort || parsed.sequence != kSequence ||
       parsed.acknowledgment != kAcknowledgment || parsed.flags != (tcp::kAck | tcp::kPsh) ||
       parsed.window != kWindow || parsed.payload_length != sizeof(payload) || parsed.payload[0] != payload[0]) {
        panic::halt("TCP smoke test could not parse a segment");
    }

    segment[segment_length - 1] ^= 1;
    if(tcp::parse_segment(kLocalIp, kPeerIp, segment, segment_length, &parsed)) {
        panic::halt("TCP smoke test accepted a corrupted checksum");
    }
    segment[segment_length - 1] ^= 1;

    if(tcp::parse_segment(kLocalIp, kWrongPeerIp, segment, segment_length, &parsed)) {
        panic::halt("TCP smoke test accepted an incorrect pseudo-header");
    }

    segment[12] = 0x40;
    if(tcp::parse_segment(kLocalIp, kPeerIp, segment, segment_length, &parsed)) {
        panic::halt("TCP smoke test accepted an invalid data offset");
    }
    segment[12] = 0x50;

    static uint8_t malformed[tcp::kHeaderLength] = {};
    if(tcp::parse_segment(kLocalIp, kPeerIp, malformed, sizeof(malformed), &parsed)) {
        panic::halt("TCP smoke test accepted an invalid checksum");
    }

    if(tcp::build_segment(
           kLocalIp,
           kPeerIp,
           kSourcePort,
           kDestinationPort,
           0,
           0,
           tcp::kSyn | tcp::kFin,
           kWindow,
           kUrgentPointer,
           nullptr,
           0,
           segment,
           sizeof(segment),
           &segment_length
       ) != tcp::Status::InvalidArgument ||
       tcp::build_segment(
           kLocalIp,
           kPeerIp,
           kSourcePort,
           kDestinationPort,
           0,
           0,
           tcp::kSyn,
           kWindow,
           kUrgentPointer,
           nullptr,
           0,
           segment,
           tcp::kHeaderLength - 1,
           &segment_length
       ) != tcp::Status::BufferTooSmall) {
        panic::halt("TCP smoke test accepted invalid build parameters");
    }
    serial::write("TCP segment, checksum, and validation smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//           hicpp-member-init, hicpp-signed-bitwise)
