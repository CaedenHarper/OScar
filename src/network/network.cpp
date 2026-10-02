#include "network.hpp"

#include "dns.hpp"
#include "ethernet.hpp"
#include "icmp.hpp"
#include "interrupts.hpp"
#include "timer.hpp"
#include "udp.hpp"
#include "virtio_net.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace network {

namespace {

constexpr arp::Ipv4Address kLocalAddress = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kNetmask = {{255, 255, 255, 0}};
constexpr arp::Ipv4Address kGateway = {{10, 0, 2, 2}};
// QEMU user-mode networking exposes its forwarding DNS proxy at this address.
// Keeping it here makes the initial resolver deterministic while leaving room
// for DHCP or /etc/resolv.conf configuration later.
constexpr arp::Ipv4Address kDnsServer = {{10, 0, 2, 3}};
constexpr ethernet::MacAddress kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr uint16_t kMaximumFrameLength = ethernet::kMaximumFrameLength;
constexpr uint16_t kMaximumTcpPayload =
    ethernet::kMaximumPayloadLength - ipv4::kMinimumHeaderLength - tcp::kHeaderLength;
constexpr uint16_t kFirstTcpEphemeralPort = 49152;

virtio_net::Device* g_device = nullptr;
arp::Interface g_arp_interface;
ipv4::Interface g_ipv4_interface;
bool g_initialized = false;
uint16_t g_sequence = 0;
uint16_t g_dns_identifier = 1;
uint16_t g_next_tcp_port = kFirstTcpEphemeralPort;
uint32_t g_tcp_sequence = 1;

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

bool send_tcp_segment(
    void*,
    const tcp_connection::Connection& connection,
    uint32_t sequence,
    uint32_t acknowledgment,
    uint8_t flags,
    const void* payload,
    uint16_t payload_length
) {
    if(payload_length > kMaximumTcpPayload) {
        return false;
    }
    static uint8_t packet[ethernet::kMaximumPayloadLength];
    uint16_t packet_length = 0;
    if(tcp::build_segment(
           connection.local_address,
           connection.remote_address,
           connection.local_port,
           connection.remote_port,
           sequence,
           acknowledgment,
           flags,
           4096,
           0,
           payload,
           payload_length,
           packet,
           sizeof(packet),
           &packet_length
       ) != tcp::Status::Success) {
        return false;
    }
    static uint8_t frame[kMaximumFrameLength];
    uint16_t frame_length = 0;
    return ipv4::build_frame(
               g_ipv4_interface,
               connection.remote_address,
               ipv4::Protocol::Tcp,
               packet,
               packet_length,
               frame,
               sizeof(frame),
               &frame_length,
               timer::ticks()
           ) == ipv4::FrameStatus::Success &&
           g_device->send(g_device->context, frame, frame_length) == virtio_net::Status::Success;
}

bool process_tcp_input(tcp_connection::Connection* connection, uint64_t now, TcpStatus* result) {
    static uint8_t frame[kMaximumFrameLength];
    uint16_t length = 0;
    const virtio_net::Status receive_status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
    if(receive_status == virtio_net::Status::NotReady) {
        asm volatile("pause");
        return false;
    }
    if(receive_status != virtio_net::Status::Success) {
        *result = TcpStatus::IoError;
        return true;
    }
    ethernet::FrameView ethernet_frame;
    if(!ethernet::parse_frame(frame, length, &ethernet_frame)) {
        return false;
    }
    if(ethernet_frame.type == ethernet::EtherType::Arp) {
        *result = process_incoming_arp(ethernet_frame, now) ? TcpStatus::Success : TcpStatus::IoError;
        return false;
    }
    if(ethernet_frame.type != ethernet::EtherType::Ipv4 ||
       !ethernet::is_for_us(ethernet_frame, g_arp_interface.hardware)) {
        return false;
    }
    ipv4::PacketView ipv4_packet;
    if(!ipv4::parse_packet(ethernet_frame.payload, ethernet_frame.payload_length, &ipv4_packet) ||
       ipv4_packet.protocol != ipv4::Protocol::Tcp || !ipv4::is_for_us(g_ipv4_interface, ipv4_packet) ||
       !arp::addresses_equal(ipv4_packet.source, connection->remote_address) ||
       !arp::addresses_equal(ipv4_packet.destination, connection->local_address)) {
        return false;
    }
    tcp::SegmentView segment;
    if(!tcp::parse_segment(
           ipv4_packet.source, ipv4_packet.destination, ipv4_packet.payload, ipv4_packet.payload_length, &segment
       ) ||
       segment.source_port != connection->remote_port || segment.destination_port != connection->local_port) {
        return false;
    }
    const tcp_connection::Result connection_result =
        tcp_connection::process(connection, send_tcp_segment, nullptr, segment, now);
    if(connection_result == tcp_connection::Result::Reset) {
        *result = TcpStatus::Reset;
    } else if(connection_result == tcp_connection::Result::IoError) {
        *result = TcpStatus::IoError;
    }
    return true;
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
    // int 0x80 uses an interrupt gate, so IF is cleared on syscall entry. This
    // operation polls for network input and uses timer ticks for both its
    // deadline and elapsed time; leave interrupts enabled while it waits or a
    // remote reply would always appear to take zero milliseconds.
    interrupts::enable();
    const PingStatus resolution = resolve(destination, timeout_ticks);
    if(resolution != PingStatus::Success) {
        return resolution;
    }

    static uint8_t request_payload[ipv4::kMaximumPacketLength];
    uint16_t request_payload_length = 0;
    constexpr uint8_t kDefaultPayload[] = {'O', 'S', 'c', 'a', 'r'};
    const uint16_t sequence = g_sequence++;
    if(icmp::build_echo_request(
           identifier,
           sequence,
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
    const uint64_t start = timer::ticks();
    if(g_device->send(g_device->context, request_frame, request_frame_length) != virtio_net::Status::Success) {
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
           reply.identifier != identifier || reply.sequence != sequence) {
            continue;
        }
        *elapsed_ticks = timer::ticks() - start;
        return PingStatus::Success;
    }
    return PingStatus::Timeout;
}

ResolveStatus resolve_hostname(const char* hostname, uint64_t timeout_ticks, arp::Ipv4Address* address) {
    if(!g_initialized || g_device == nullptr) {
        return ResolveStatus::NotInitialized;
    }
    if(hostname == nullptr || address == nullptr || hostname[0] == '\0') {
        return ResolveStatus::InvalidArgument;
    }
    // Like ping, DNS waits on both packet input and timer-driven deadlines;
    // syscall entry has IF cleared and would otherwise freeze that clock.
    interrupts::enable();
    const PingStatus resolution = resolve(kDnsServer, timeout_ticks);
    if(resolution != PingStatus::Success) {
        switch(resolution) {
            case PingStatus::Timeout:
                return ResolveStatus::Timeout;
            case PingStatus::InvalidArgument:
                return ResolveStatus::InvalidArgument;
            case PingStatus::NotInitialized:
                return ResolveStatus::NotInitialized;
            case PingStatus::AddressUnreachable:
            case PingStatus::IoError:
                return ResolveStatus::IoError;
            case PingStatus::Success:
                break;
        }
    }

    static uint8_t query_payload[512];
    uint16_t query_length = 0;
    const uint16_t identifier = g_dns_identifier++;
    if(dns::build_query(hostname, identifier, query_payload, sizeof(query_payload), &query_length) !=
       dns::Status::Success) {
        return ResolveStatus::InvalidArgument;
    }
    static uint8_t request_frame[kMaximumFrameLength];
    uint16_t request_frame_length = 0;
    constexpr uint16_t kDnsSourcePort = 49152;
    if(udp::build_frame(
           g_ipv4_interface,
           kDnsServer,
           kDnsSourcePort,
           53,
           query_payload,
           query_length,
           request_frame,
           sizeof(request_frame),
           &request_frame_length,
           timer::ticks()
       ) != udp::Status::Success ||
       g_device->send(g_device->context, request_frame, request_frame_length) != virtio_net::Status::Success) {
        return ResolveStatus::IoError;
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
            return ResolveStatus::IoError;
        }
        ethernet::FrameView received;
        if(!ethernet::parse_frame(frame, length, &received)) {
            continue;
        }
        if(received.type == ethernet::EtherType::Arp) {
            if(!process_incoming_arp(received, timer::ticks())) {
                return ResolveStatus::IoError;
            }
            continue;
        }
        if(received.type != ethernet::EtherType::Ipv4 || !ethernet::is_for_us(received, g_arp_interface.hardware)) {
            continue;
        }
        ipv4::PacketView packet;
        if(!ipv4::parse_packet(received.payload, received.payload_length, &packet) ||
           packet.protocol != ipv4::Protocol::Udp || !ipv4::is_for_us(g_ipv4_interface, packet) ||
           !arp::addresses_equal(packet.source, kDnsServer)) {
            continue;
        }
        udp::DatagramView datagram;
        if(!udp::parse_packet(kDnsServer, g_ipv4_interface.address, packet.payload, packet.payload_length, &datagram) ||
           datagram.source_port != 53 || datagram.destination_port != kDnsSourcePort) {
            continue;
        }
        const dns::Status result = dns::parse_response(datagram.payload, datagram.payload_length, identifier, address);
        if(result == dns::Status::Success) {
            return ResolveStatus::Success;
        }
        if(result == dns::Status::NameNotFound) {
            return ResolveStatus::NameNotFound;
        }
        if(result == dns::Status::ServerFailure || result == dns::Status::NoAddress) {
            return ResolveStatus::IoError;
        }
    }
    return ResolveStatus::Timeout;
}

TcpStatus tcp_connect(
    tcp_connection::Connection* connection,
    arp::Ipv4Address destination,
    uint16_t destination_port,
    uint64_t timeout_ticks
) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr || destination_port == 0) {
        return TcpStatus::InvalidArgument;
    }
    interrupts::enable();
    const PingStatus resolution = resolve(destination, timeout_ticks);
    if(resolution != PingStatus::Success) {
        return resolution == PingStatus::Timeout              ? TcpStatus::Timeout
               : resolution == PingStatus::AddressUnreachable ? TcpStatus::AddressUnreachable
               : resolution == PingStatus::NotInitialized     ? TcpStatus::NotInitialized
                                                              : TcpStatus::IoError;
    }
    const uint16_t local_port = g_next_tcp_port++;
    if(g_next_tcp_port == 0) {
        g_next_tcp_port = kFirstTcpEphemeralPort;
    }
    tcp_connection::initialize(
        connection, g_ipv4_interface.address, destination, local_port, destination_port, g_tcp_sequence
    );
    g_tcp_sequence += 0x10001U;
    const uint64_t start = timer::ticks();
    if(tcp_connection::open(connection, send_tcp_segment, nullptr, start) != tcp_connection::Result::Success) {
        return TcpStatus::IoError;
    }
    while(time_remaining(start, timeout_ticks)) {
        const uint64_t now = timer::ticks();
        const tcp_connection::Result retry = tcp_connection::poll(connection, send_tcp_segment, nullptr, now);
        if(retry == tcp_connection::Result::Timeout) {
            return TcpStatus::Timeout;
        }
        TcpStatus input_result = TcpStatus::Success;
        (void)process_tcp_input(connection, now, &input_result);
        if(input_result != TcpStatus::Success) {
            return input_result;
        }
        if(connection->state == tcp_connection::State::Established) {
            return TcpStatus::Success;
        }
        if(connection->state == tcp_connection::State::Reset) {
            return TcpStatus::Reset;
        }
    }
    return TcpStatus::Timeout;
}

TcpStatus tcp_send(tcp_connection::Connection* connection, const void* data, uint16_t length, uint64_t timeout_ticks) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr || data == nullptr || length == 0) {
        return TcpStatus::InvalidArgument;
    }
    if(connection->state != tcp_connection::State::Established) {
        return TcpStatus::NotConnected;
    }
    interrupts::enable();
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint16_t sent = 0;
    const uint64_t start = timer::ticks();
    while(sent < length) {
        const uint16_t chunk = length - sent > kMaximumTcpPayload ? kMaximumTcpPayload : length - sent;
        const tcp_connection::Result send_result =
            tcp_connection::send(connection, send_tcp_segment, nullptr, bytes + sent, chunk, timer::ticks());
        if(send_result == tcp_connection::Result::Reset) {
            return TcpStatus::Reset;
        }
        if(send_result != tcp_connection::Result::Success) {
            return send_result == tcp_connection::Result::WouldBlock ? TcpStatus::IoError : TcpStatus::IoError;
        }
        while(connection->send_unacknowledged != connection->send_next) {
            if(!time_remaining(start, timeout_ticks)) {
                return TcpStatus::Timeout;
            }
            const uint64_t now = timer::ticks();
            if(tcp_connection::poll(connection, send_tcp_segment, nullptr, now) == tcp_connection::Result::Timeout) {
                return TcpStatus::Timeout;
            }
            TcpStatus input_result = TcpStatus::Success;
            (void)process_tcp_input(connection, now, &input_result);
            if(input_result != TcpStatus::Success) {
                return input_result;
            }
            if(connection->state == tcp_connection::State::Reset) {
                return TcpStatus::Reset;
            }
        }
        sent = static_cast<uint16_t>(sent + chunk);
    }
    return TcpStatus::Success;
}

TcpStatus tcp_receive(
    tcp_connection::Connection* connection,
    void* output,
    uint16_t capacity,
    uint16_t* length,
    uint64_t timeout_ticks
) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr || output == nullptr || length == nullptr || capacity == 0) {
        return TcpStatus::InvalidArgument;
    }
    if(connection->state != tcp_connection::State::Established && connection->state != tcp_connection::State::FinWait &&
       connection->state != tcp_connection::State::TimeWait) {
        return TcpStatus::NotConnected;
    }
    interrupts::enable();
    *length = 0;
    const uint64_t start = timer::ticks();
    while(time_remaining(start, timeout_ticks)) {
        const tcp_connection::Result receive_result = tcp_connection::receive(connection, output, capacity, length);
        if(receive_result == tcp_connection::Result::Success) {
            return TcpStatus::Success;
        }
        if(receive_result == tcp_connection::Result::Closed) {
            return TcpStatus::Success;
        }
        if(receive_result == tcp_connection::Result::Reset) {
            return TcpStatus::Reset;
        }
        const uint64_t now = timer::ticks();
        if(tcp_connection::poll(connection, send_tcp_segment, nullptr, now) == tcp_connection::Result::Timeout) {
            return TcpStatus::Timeout;
        }
        TcpStatus input_result = TcpStatus::Success;
        (void)process_tcp_input(connection, now, &input_result);
        if(input_result != TcpStatus::Success) {
            return input_result;
        }
    }
    return TcpStatus::Timeout;
}

TcpStatus tcp_close(tcp_connection::Connection* connection, uint64_t timeout_ticks) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr) {
        return TcpStatus::InvalidArgument;
    }
    if(connection->state == tcp_connection::State::Closed) {
        return TcpStatus::Success;
    }
    interrupts::enable();
    const uint64_t start = timer::ticks();
    if(connection->state == tcp_connection::State::Established &&
       tcp_connection::close(connection, send_tcp_segment, nullptr, start) != tcp_connection::Result::Success) {
        return TcpStatus::IoError;
    }
    while(time_remaining(start, timeout_ticks)) {
        if(connection->state == tcp_connection::State::Closed || connection->state == tcp_connection::State::TimeWait) {
            return TcpStatus::Success;
        }
        const uint64_t now = timer::ticks();
        if(tcp_connection::poll(connection, send_tcp_segment, nullptr, now) == tcp_connection::Result::Timeout) {
            return TcpStatus::Timeout;
        }
        TcpStatus input_result = TcpStatus::Success;
        (void)process_tcp_input(connection, now, &input_result);
        if(input_result == TcpStatus::Reset) {
            return TcpStatus::Reset;
        }
        if(input_result == TcpStatus::IoError) {
            return TcpStatus::IoError;
        }
    }
    return TcpStatus::Timeout;
}

} // namespace network

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
