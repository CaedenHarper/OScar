#include "arp.hpp"
#include "ethernet.hpp"
#include "ipv4.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "udp.hpp"

#include <stdint.h>

namespace self_tests_detail {

namespace {

constexpr ethernet::MacAddress kLocalMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
constexpr ethernet::MacAddress kPeerMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
constexpr arp::Ipv4Address kLocalIp = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kPeerIp = {{10, 0, 2, 20}};
constexpr arp::Ipv4Address kNetmask = {{255, 255, 255, 0}};
constexpr arp::Ipv4Address kGateway = {{10, 0, 2, 2}};

} // namespace

void test_udp() {
    arp::Interface arp_interface;
    arp::initialize(&arp_interface, kLocalMac, kLocalIp);
    arp::cache_insert(&arp_interface.cache, kPeerIp, kPeerMac, 10);
    ipv4::Interface interface;
    ipv4::initialize(&interface, &arp_interface, kLocalIp, kNetmask, kGateway);

    static uint8_t payload[] = {'D', 'N', 'S'};
    static uint8_t datagram[ethernet::kMaximumPayloadLength];
    uint16_t datagram_length = 0;
    if(udp::build_packet(
           kLocalIp, kPeerIp, 40000, 53, payload, sizeof(payload), datagram, sizeof(datagram), &datagram_length
       ) != udp::Status::Success ||
       datagram_length != udp::kHeaderLength + sizeof(payload) || datagram[0] != 0x9c || datagram[1] != 0x40 ||
       datagram[2] != 0 || datagram[3] != 53) {
        panic::halt("UDP smoke test could not build a datagram");
    }

    udp::DatagramView parsed;
    if(!udp::parse_packet(kLocalIp, kPeerIp, datagram, datagram_length, &parsed) || parsed.source_port != 40000 ||
       parsed.destination_port != 53 || parsed.payload_length != sizeof(payload) || parsed.payload[0] != payload[0]) {
        panic::halt("UDP smoke test could not parse a datagram");
    }

    datagram[7] ^= 1;
    if(udp::parse_packet(kLocalIp, kPeerIp, datagram, datagram_length, &parsed)) {
        panic::halt("UDP smoke test accepted a corrupted checksum");
    }
    datagram[7] ^= 1;

    static uint8_t frame[ethernet::kMaximumFrameLength];
    uint16_t frame_length = 0;
    if(udp::build_frame(
           interface, kPeerIp, 40000, 53, payload, sizeof(payload), frame, sizeof(frame), &frame_length, 10
       ) != udp::Status::Success) {
        panic::halt("UDP smoke test could not build a routed frame");
    }
    ethernet::FrameView ethernet_frame;
    ipv4::PacketView ipv4_packet;
    if(!ethernet::parse_frame(frame, frame_length, &ethernet_frame) ||
       !ipv4::parse_packet(ethernet_frame.payload, ethernet_frame.payload_length, &ipv4_packet) ||
       ipv4_packet.protocol != ipv4::Protocol::Udp ||
       !udp::parse_packet(kLocalIp, kPeerIp, ipv4_packet.payload, ipv4_packet.payload_length, &parsed) ||
       parsed.destination_port != 53) {
        panic::halt("UDP smoke test produced an invalid routed frame");
    }

    static uint8_t malformed[udp::kHeaderLength];
    if(udp::parse_packet(kLocalIp, kPeerIp, malformed, sizeof(malformed), &parsed)) {
        panic::halt("UDP smoke test accepted a malformed datagram");
    }
    serial::write("UDP datagram, checksum, and IPv4 framing smoke test passed.\n");
}

} // namespace self_tests_detail
