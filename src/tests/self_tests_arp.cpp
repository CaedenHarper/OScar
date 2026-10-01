#include "arp.hpp"
#include "ethernet.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

namespace self_tests_detail {

namespace {

constexpr ethernet::MacAddress kLocalMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
constexpr ethernet::MacAddress kPeerMac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
constexpr arp::Ipv4Address kLocalIp = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kPeerIp = {{10, 0, 2, 2}};

} // namespace

void test_arp() {
    arp::Interface interface;
    arp::initialize(&interface, kLocalMac, kLocalIp);

    static uint8_t payload[arp::kPacketLength];
    uint16_t payload_length = 0;
    if(!arp::build_request(interface, kPeerIp, payload, sizeof(payload), &payload_length) ||
       payload_length != arp::kPacketLength || payload[0] != 0 || payload[1] != 1 || payload[2] != 0x08 ||
       payload[3] != 0 || payload[6] != 0 || payload[7] != 1 || payload[14] != 10 || payload[17] != 15) {
        panic::halt("ARP smoke test could not build a request");
    }

    arp::PacketView request;
    if(!arp::parse_packet(payload, payload_length, &request) || request.opcode != arp::Opcode::Request ||
       !arp::addresses_equal(request.sender_protocol, kLocalIp) ||
       !arp::addresses_equal(request.target_protocol, kPeerIp)) {
        panic::halt("ARP smoke test could not parse its request");
    }

    arp::Interface peer;
    arp::initialize(&peer, kPeerMac, kPeerIp);
    static uint8_t peer_payload[arp::kPacketLength];
    uint16_t peer_payload_length = 0;
    static uint8_t frame[ethernet::kMaximumFrameLength];
    uint16_t frame_length = 0;
    if(!arp::build_request(peer, kLocalIp, peer_payload, sizeof(peer_payload), &peer_payload_length) ||
       !ethernet::build_frame(
           frame,
           sizeof(frame),
           kLocalMac,
           kPeerMac,
           ethernet::EtherType::Arp,
           peer_payload,
           peer_payload_length,
           &frame_length
       )) {
        panic::halt("ARP smoke test could not prepare a peer request");
    }

    ethernet::FrameView frame_view;
    if(!ethernet::parse_frame(frame, frame_length, &frame_view)) {
        panic::halt("ARP smoke test could not parse a peer request frame");
    }
    static uint8_t response[ethernet::kMaximumFrameLength];
    uint16_t response_length = 0;
    if(!arp::process_frame(&interface, frame_view, response, sizeof(response), &response_length, 10)) {
        panic::halt("ARP smoke test did not produce a reply");
    }

    ethernet::FrameView response_frame;
    arp::PacketView reply;
    if(!ethernet::parse_frame(response, response_length, &response_frame) ||
       !arp::parse_packet(response_frame.payload, response_frame.payload_length, &reply) ||
       reply.opcode != arp::Opcode::Reply || !ethernet::addresses_equal(response_frame.destination, kPeerMac) ||
       !ethernet::addresses_equal(reply.sender_hardware, kLocalMac) ||
       !arp::addresses_equal(reply.sender_protocol, kLocalIp)) {
        panic::halt("ARP smoke test produced an invalid reply");
    }

    ethernet::MacAddress cached = {};
    if(!arp::cache_lookup(interface.cache, kPeerIp, 10, &cached) || !ethernet::addresses_equal(cached, kPeerMac) ||
       arp::cache_lookup(interface.cache, kPeerIp, 610, &cached)) {
        panic::halt("ARP smoke test cache expiration failed");
    }

    static uint8_t malformed[arp::kPacketLength];
    if(arp::parse_packet(malformed, sizeof(malformed), &reply) || arp::parse_packet(payload, 10, &reply)) {
        panic::halt("ARP smoke test accepted malformed packets");
    }
    serial::write("ARP framing and cache smoke test passed.\n");
}

} // namespace self_tests_detail
