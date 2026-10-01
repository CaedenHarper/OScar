#include "arp.hpp"
#include "ethernet.hpp"
#include "icmp.hpp"
#include "ipv4.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

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

void test_icmp() {
    arp::Interface local_arp;
    arp::initialize(&local_arp, kLocalMac, kLocalIp);
    arp::Interface peer_arp;
    arp::initialize(&peer_arp, kPeerMac, kPeerIp);

    ipv4::Interface local;
    ipv4::initialize(&local, &local_arp, kLocalIp, kNetmask, kGateway);
    ipv4::Interface peer;
    ipv4::initialize(&peer, &peer_arp, kPeerIp, kNetmask, kGateway);
    arp::cache_insert(&local_arp.cache, kPeerIp, kPeerMac, 10);

    static uint8_t echo_payload[] = {0xde, 0xad, 0xbe, 0xef};
    static uint8_t icmp_packet[ipv4::kMaximumPacketLength];
    uint16_t icmp_length = 0;
    if(icmp::build_echo_request(
           0x1234, 7, echo_payload, sizeof(echo_payload), icmp_packet, sizeof(icmp_packet), &icmp_length
       ) != icmp::Status::Success) {
        panic::halt("ICMP smoke test could not build an echo request");
    }

    icmp::EchoView request;
    if(!icmp::parse_echo(icmp_packet, icmp_length, &request) || request.type != icmp::Type::EchoRequest ||
       request.identifier != 0x1234 || request.sequence != 7 || request.payload_length != sizeof(echo_payload) ||
       request.payload[0] != echo_payload[0]) {
        panic::halt("ICMP smoke test could not parse an echo request");
    }

    icmp_packet[2] ^= 1;
    if(icmp::parse_echo(icmp_packet, icmp_length, &request)) {
        panic::halt("ICMP smoke test accepted a corrupted checksum");
    }
    icmp_packet[2] ^= 1;

    static uint8_t peer_ipv4[ipv4::kMaximumPacketLength];
    uint16_t peer_ipv4_length = 0;
    if(ipv4::build_packet(
           peer,
           kLocalIp,
           ipv4::Protocol::Icmp,
           icmp_packet,
           icmp_length,
           peer_ipv4,
           sizeof(peer_ipv4),
           &peer_ipv4_length
       ) != ipv4::FrameStatus::Success) {
        panic::halt("ICMP smoke test could not build an IPv4 request");
    }
    ipv4::PacketView incoming;
    if(!ipv4::parse_packet(peer_ipv4, peer_ipv4_length, &incoming)) {
        panic::halt("ICMP smoke test could not parse an IPv4 request");
    }

    static uint8_t reply_frame[ethernet::kMaximumFrameLength];
    uint16_t reply_frame_length = 0;
    if(icmp::process_echo_request(local, incoming, reply_frame, sizeof(reply_frame), &reply_frame_length, 10) !=
       icmp::Status::Success) {
        panic::halt("ICMP smoke test did not build an echo reply");
    }

    ethernet::FrameView ethernet_reply;
    ipv4::PacketView ipv4_reply;
    icmp::EchoView reply;
    if(!ethernet::parse_frame(reply_frame, reply_frame_length, &ethernet_reply) ||
       !ethernet::addresses_equal(ethernet_reply.destination, kPeerMac) ||
       !ipv4::parse_packet(ethernet_reply.payload, ethernet_reply.payload_length, &ipv4_reply) ||
       !arp::addresses_equal(ipv4_reply.destination, kPeerIp) ||
       icmp::parse_echo(ipv4_reply.payload, ipv4_reply.payload_length, &reply) == false ||
       reply.type != icmp::Type::EchoReply || reply.identifier != 0x1234 || reply.sequence != 7 ||
       reply.payload_length != sizeof(echo_payload) || reply.payload[3] != echo_payload[3]) {
        panic::halt("ICMP smoke test produced an invalid echo reply");
    }

    incoming.destination = kPeerIp;
    if(icmp::process_echo_request(local, incoming, reply_frame, sizeof(reply_frame), &reply_frame_length, 10) !=
       icmp::Status::Ignored) {
        panic::halt("ICMP smoke test replied to a non-local request");
    }

    static uint8_t unsupported[icmp::kHeaderLength] = {};
    unsupported[0] = 3;
    icmp::EchoView unsupported_view;
    if(icmp::parse_echo(unsupported, sizeof(unsupported), &unsupported_view)) {
        panic::halt("ICMP smoke test accepted an unsupported message");
    }
    serial::write("ICMP echo request/reply smoke test passed.\n");
}

} // namespace self_tests_detail
