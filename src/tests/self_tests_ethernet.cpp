#include "ethernet.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

// Raw Ethernet fixtures deliberately use byte offsets, array-to-pointer calls,
// and protocol literals so malformed frames remain explicit and reviewable.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//             hicpp-member-init, hicpp-signed-bitwise)

namespace self_tests_detail {

namespace {

constexpr ethernet::MacAddress kDestination = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
constexpr ethernet::MacAddress kSource = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
constexpr ethernet::MacAddress kOther = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x03}};
constexpr ethernet::MacAddress kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

} // namespace

void test_ethernet() {
    static uint8_t payload[] = {0x01, 0x02, 0x03};
    static uint8_t frame[ethernet::kMaximumFrameLength];
    uint16_t frame_length = 0;
    if(!ethernet::build_frame(
           frame,
           sizeof(frame),
           kDestination,
           kSource,
           ethernet::EtherType::Arp,
           payload,
           sizeof(payload),
           &frame_length
       ) ||
       frame_length != ethernet::kHeaderLength + ethernet::kMinimumPayloadLength || frame[12] != 0x08 ||
       frame[13] != 0x06 || frame[ethernet::kHeaderLength + sizeof(payload)] != 0) {
        panic::halt("Ethernet framing smoke test could not build a padded frame");
    }

    ethernet::FrameView parsed = {};
    if(!ethernet::parse_frame(frame, frame_length, &parsed) || !ethernet::addresses_equal(parsed.source, kSource) ||
       !ethernet::addresses_equal(parsed.destination, kDestination) || parsed.type != ethernet::EtherType::Arp ||
       parsed.payload_length != ethernet::kMinimumPayloadLength || parsed.payload[0] != payload[0] ||
       ethernet::is_for_us(parsed, kOther)) {
        panic::halt("Ethernet framing smoke test parsed an invalid frame");
    }

    parsed.destination = kBroadcast;
    if(!ethernet::is_broadcast(kBroadcast) || !ethernet::is_for_us(parsed, kOther) ||
       !ethernet::is_multicast({{0x01, 0x00, 0x5e, 0, 0, 1}})) {
        panic::halt("Ethernet framing smoke test rejected a broadcast or multicast address");
    }

    if(ethernet::parse_frame(frame, ethernet::kHeaderLength - 1, &parsed) ||
       ethernet::build_frame(
           frame,
           ethernet::kHeaderLength,
           kDestination,
           kSource,
           ethernet::EtherType::Ipv4,
           payload,
           sizeof(payload),
           &frame_length
       ) ||
       ethernet::build_frame(
           frame, sizeof(frame), kDestination, kSource, ethernet::EtherType::Ipv4, payload, 1501, &frame_length
       )) {
        panic::halt("Ethernet framing smoke test accepted an invalid boundary case");
    }
    serial::write("Ethernet framing smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//           hicpp-member-init, hicpp-signed-bitwise)
