#include "arp.hpp"
#include "ethernet.hpp"
#include "ipv4.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

// IPv4 tests intentionally construct and corrupt raw packets to verify header
// validation, checksum handling, and routing decisions at exact byte offsets.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//             hicpp-member-init, hicpp-signed-bitwise, fuchsia-default-arguments-calls)

namespace self_tests_detail {

namespace {

constexpr ethernet::MacAddress kLocalMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
constexpr ethernet::MacAddress kPeerMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
constexpr ethernet::MacAddress kGatewayMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x03}};
constexpr arp::Ipv4Address kLocalIp = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kPeerIp = {{10, 0, 2, 20}};
constexpr arp::Ipv4Address kGatewayIp = {{10, 0, 2, 2}};
constexpr arp::Ipv4Address kRemoteIp = {{192, 0, 2, 1}};
constexpr arp::Ipv4Address kNetmask = {{255, 255, 255, 0}};

} // namespace

void test_ipv4() {
    arp::Interface arp_interface;
    arp::initialize(&arp_interface, kLocalMac, kLocalIp);
    ipv4::Interface interface;
    ipv4::initialize(&interface, &arp_interface, kLocalIp, kNetmask, kGatewayIp);

    static uint8_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    static uint8_t packet[ipv4::kMaximumPacketLength];
    uint16_t packet_length = 0;
    if(ipv4::build_packet(
           interface, kPeerIp, ipv4::Protocol::Icmp, payload, sizeof(payload), packet, sizeof(packet), &packet_length
       ) != ipv4::FrameStatus::Success ||
       packet_length != ipv4::kMinimumHeaderLength + sizeof(payload) || packet[0] != 0x45 || packet[8] != 64 ||
       packet[9] != static_cast<uint8_t>(ipv4::Protocol::Icmp)) {
        panic::halt("IPv4 smoke test could not build a packet");
    }

    ipv4::PacketView parsed;
    if(!ipv4::parse_packet(packet, packet_length, &parsed) || parsed.total_length != packet_length ||
       parsed.payload_length != sizeof(payload) || parsed.payload[0] != payload[0] ||
       !arp::addresses_equal(parsed.source, kLocalIp) || !arp::addresses_equal(parsed.destination, kPeerIp) ||
       parsed.identification != 0) {
        panic::halt("IPv4 smoke test could not parse a valid packet");
    }

    packet[10] ^= 0x01;
    if(ipv4::parse_packet(packet, packet_length, &parsed)) {
        panic::halt("IPv4 smoke test accepted a corrupted checksum");
    }
    packet[10] ^= 0x01;

    static uint8_t malformed[ipv4::kMinimumHeaderLength] = {};
    malformed[0] = 0x65;
    if(ipv4::parse_packet(malformed, sizeof(malformed), &parsed)) {
        panic::halt("IPv4 smoke test accepted an unsupported version");
    }

    ipv4::Route selected_route;
    if(!ipv4::route(interface, kLocalIp, &selected_route) || selected_route.kind != ipv4::RouteKind::Local ||
       !ipv4::route(interface, kPeerIp, &selected_route) || selected_route.kind != ipv4::RouteKind::Direct ||
       !ipv4::route(interface, kRemoteIp, &selected_route) || selected_route.kind != ipv4::RouteKind::Gateway ||
       !ipv4::is_same_subnet(kLocalIp, kPeerIp, kNetmask) || ipv4::is_same_subnet(kLocalIp, kRemoteIp, kNetmask)) {
        panic::halt("IPv4 smoke test selected an incorrect route");
    }

    arp::cache_insert(&arp_interface.cache, kPeerIp, kPeerMac, 10, arp::kDefaultCacheLifetime);
    arp::cache_insert(&arp_interface.cache, kGatewayIp, kGatewayMac, 10, arp::kDefaultCacheLifetime);
    static uint8_t frame[ethernet::kMaximumFrameLength];
    uint16_t frame_length = 0;
    if(ipv4::build_frame(
           interface, kRemoteIp, ipv4::Protocol::Udp, payload, sizeof(payload), frame, sizeof(frame), &frame_length, 10
       ) != ipv4::FrameStatus::Success) {
        panic::halt("IPv4 smoke test could not route an Ethernet frame");
    }

    ethernet::FrameView ethernet_frame;
    ipv4::PacketView routed_packet;
    parsed.destination = kLocalIp;
    if(!ethernet::parse_frame(frame, frame_length, &ethernet_frame) ||
       !ethernet::addresses_equal(ethernet_frame.destination, kGatewayMac) ||
       !ipv4::parse_packet(ethernet_frame.payload, ethernet_frame.payload_length, &routed_packet) ||
       !arp::addresses_equal(routed_packet.destination, kRemoteIp) || routed_packet.protocol != ipv4::Protocol::Udp ||
       !ipv4::is_for_us(interface, parsed)) {
        panic::halt("IPv4 smoke test produced an invalid routed frame");
    }

    if(ipv4::build_frame(
           interface,
           kRemoteIp,
           ipv4::Protocol::Icmp,
           payload,
           sizeof(payload),
           frame,
           sizeof(frame),
           &frame_length,
           700
       ) != ipv4::FrameStatus::AddressUnreachable) {
        panic::halt("IPv4 smoke test ignored an expired ARP mapping");
    }
    serial::write("IPv4 packet, checksum, and routing smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-type-member-init,
//           hicpp-member-init, hicpp-signed-bitwise, fuchsia-default-arguments-calls)
