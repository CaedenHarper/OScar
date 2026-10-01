#include "network.hpp"

#include "ethernet.hpp"
#include "icmp.hpp"
#include "timer.hpp"
#include "virtio_net.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace network {

namespace {

constexpr arp::Ipv4Address kLocalAddress = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kNetmask = {{255, 255, 255, 0}};
constexpr arp::Ipv4Address kGateway = {{10, 0, 2, 2}};
constexpr ethernet::MacAddress kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr uint16_t kMaximumFrameLength = ethernet::kMaximumFrameLength;

virtio_net::Device* g_device = nullptr;
arp::Interface g_arp_interface;
ipv4::Interface g_ipv4_interface;
bool g_initialized = false;
uint16_t g_sequence = 0;

void copy_mac(ethernet::MacAddress* destination, virtio_net::MacAddress source) {
    for(uint8_t index = 0; index < sizeof(destination->bytes); ++index) {
        destination->bytes[index] = source.bytes[index];
    }
}

bool time_remaining(uint64_t start, uint64_t timeout) {
    return timer::ticks() - start < timeout;
}

bool process_incoming_arp(const ethernet::FrameView& frame, uint64_t now) {
    static uint8_t response[kMaximumFrameLength];
    uint16_t response_length = 0;
    if(!arp::process_frame(&g_arp_interface, frame, response, sizeof(response), &response_length, now)) {
        return true;
    }
    return g_device->send(g_device->context, response, response_length) == virtio_net::Status::Success;
}

PingStatus resolve(arp::Ipv4Address destination, uint64_t timeout_ticks) {
    ipv4::Route route;
    if(!ipv4::route(g_ipv4_interface, destination, &route) || route.kind == ipv4::RouteKind::Local) {
        return PingStatus::AddressUnreachable;
    }
    ethernet::MacAddress cached;
    const uint64_t start = timer::ticks();
    if(arp::cache_lookup(g_arp_interface.cache, route.next_hop, start, &cached)) {
        return PingStatus::Success;
    }

    static uint8_t request_payload[arp::kPacketLength];
    uint16_t request_payload_length = 0;
    if(!arp::build_request(
           g_arp_interface, route.next_hop, request_payload, sizeof(request_payload), &request_payload_length
       )) {
        return PingStatus::IoError;
    }
    static uint8_t request_frame[kMaximumFrameLength];
    uint16_t request_frame_length = 0;
    if(!ethernet::build_frame(
           request_frame,
           sizeof(request_frame),
           kBroadcast,
           g_arp_interface.hardware,
           ethernet::EtherType::Arp,
           request_payload,
           request_payload_length,
           &request_frame_length
       ) ||
       g_device->send(g_device->context, request_frame, request_frame_length) != virtio_net::Status::Success) {
        return PingStatus::IoError;
    }

    static uint8_t frame[kMaximumFrameLength];
    while(time_remaining(start, timeout_ticks)) {
        uint16_t length = 0;
        const virtio_net::Status status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
        if(status == virtio_net::Status::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(status != virtio_net::Status::Success) {
            return PingStatus::IoError;
        }
        ethernet::FrameView received;
        if(!ethernet::parse_frame(frame, length, &received)) {
            continue;
        }
        if(received.type == ethernet::EtherType::Arp && !process_incoming_arp(received, timer::ticks())) {
            return PingStatus::IoError;
        }
        if(arp::cache_lookup(g_arp_interface.cache, route.next_hop, timer::ticks(), &cached)) {
            return PingStatus::Success;
        }
    }
    return PingStatus::Timeout;
}

} // namespace

bool initialize() {
    if(g_initialized) {
        return true;
    }
    if(!virtio_net::initialize()) {
        return false;
    }
    g_device = virtio_net::device();
    if(g_device == nullptr) {
        return false;
    }
    ethernet::MacAddress mac;
    copy_mac(&mac, g_device->mac);
    arp::initialize(&g_arp_interface, mac, kLocalAddress);
    ipv4::initialize(&g_ipv4_interface, &g_arp_interface, kLocalAddress, kNetmask, kGateway);
    g_initialized = true;
    return true;
}

bool is_initialized() {
    return g_initialized;
}

PingStatus ping(arp::Ipv4Address destination, uint64_t timeout_ticks, uint16_t identifier, uint64_t* elapsed_ticks) {
    if(!g_initialized || g_device == nullptr || elapsed_ticks == nullptr) {
        return PingStatus::NotInitialized;
    }
    if(destination.bytes[0] == 0 && destination.bytes[1] == 0 && destination.bytes[2] == 0 &&
       destination.bytes[3] == 0) {
        return PingStatus::InvalidArgument;
    }
    const PingStatus resolution = resolve(destination, timeout_ticks);
    if(resolution != PingStatus::Success) {
        return resolution;
    }

    static uint8_t request_payload[ipv4::kMaximumPacketLength];
    uint16_t request_payload_length = 0;
    constexpr uint8_t kDefaultPayload[] = {'O', 'S', 'c', 'a', 'r'};
    if(icmp::build_echo_request(
           identifier,
           g_sequence++,
           kDefaultPayload,
           sizeof(kDefaultPayload),
           request_payload,
           sizeof(request_payload),
           &request_payload_length
       ) != icmp::Status::Success) {
        return PingStatus::IoError;
    }

    static uint8_t request_frame[kMaximumFrameLength];
    uint16_t request_frame_length = 0;
    const ipv4::FrameStatus frame_status = ipv4::build_frame(
        g_ipv4_interface,
        destination,
        ipv4::Protocol::Icmp,
        request_payload,
        request_payload_length,
        request_frame,
        sizeof(request_frame),
        &request_frame_length,
        timer::ticks()
    );
    if(frame_status != ipv4::FrameStatus::Success) {
        return frame_status == ipv4::FrameStatus::AddressUnreachable ? PingStatus::AddressUnreachable
                                                                     : PingStatus::IoError;
    }
    if(g_device->send(g_device->context, request_frame, request_frame_length) != virtio_net::Status::Success) {
        return PingStatus::IoError;
    }

    const uint64_t start = timer::ticks();
    static uint8_t frame[kMaximumFrameLength];
    while(time_remaining(start, timeout_ticks)) {
        uint16_t length = 0;
        const virtio_net::Status status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
        if(status == virtio_net::Status::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(status != virtio_net::Status::Success) {
            return PingStatus::IoError;
        }
        ethernet::FrameView received;
        if(!ethernet::parse_frame(frame, length, &received)) {
            continue;
        }
        if(received.type == ethernet::EtherType::Arp) {
            if(!process_incoming_arp(received, timer::ticks())) {
                return PingStatus::IoError;
            }
            continue;
        }
        if(received.type != ethernet::EtherType::Ipv4 || !ethernet::is_for_us(received, g_arp_interface.hardware)) {
            continue;
        }
        ipv4::PacketView packet;
        if(!ipv4::parse_packet(received.payload, received.payload_length, &packet) ||
           packet.protocol != ipv4::Protocol::Icmp || !ipv4::is_for_us(g_ipv4_interface, packet) ||
           !arp::addresses_equal(packet.source, destination)) {
            continue;
        }
        icmp::EchoView reply;
        if(!icmp::parse_echo(packet.payload, packet.payload_length, &reply) || reply.type != icmp::Type::EchoReply ||
           reply.identifier != identifier) {
            continue;
        }
        *elapsed_ticks = timer::ticks() - start;
        return PingStatus::Success;
    }
    return PingStatus::Timeout;
}

} // namespace network

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
