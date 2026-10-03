#include "network.hpp"

#include "dhcp.hpp"
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

constexpr arp::Ipv4Address kFallbackAddress = {{10, 0, 2, 15}};
constexpr arp::Ipv4Address kFallbackNetmask = {{255, 255, 255, 0}};
constexpr arp::Ipv4Address kFallbackGateway = {{10, 0, 2, 2}};
// QEMU user-mode networking exposes its forwarding DNS proxy at this address.
// Keeping it here makes the fallback resolver deterministic while leaving room
// for /etc/resolv.conf configuration later.
constexpr arp::Ipv4Address kFallbackDnsServer = {{10, 0, 2, 3}};
constexpr arp::Ipv4Address kZeroAddress = {{0, 0, 0, 0}};
constexpr arp::Ipv4Address kBroadcastAddress = {{255, 255, 255, 255}};
constexpr ethernet::MacAddress kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
constexpr uint16_t kMaximumFrameLength = ethernet::kMaximumFrameLength;
constexpr uint16_t kMaximumTcpPayload =
    ethernet::kMaximumPayloadLength - ipv4::kMinimumHeaderLength - tcp::kHeaderLength;
constexpr uint16_t kFirstTcpEphemeralPort = 49152;
constexpr uint8_t kMaximumRegisteredTcpConnections = 16;

virtio_net::Device* g_device = nullptr;
arp::Interface g_arp_interface;
ipv4::Interface g_ipv4_interface;
bool g_initialized = false;
uint16_t g_sequence = 0;
uint16_t g_dns_identifier = 1;
uint16_t g_next_tcp_port = kFirstTcpEphemeralPort;
uint32_t g_tcp_sequence = 1;
arp::Ipv4Address g_dns_server = kFallbackDnsServer;

struct RegisteredTcpConnection {
    tcp_connection::Connection* connection;
    bool active;
};

RegisteredTcpConnection g_tcp_connections[kMaximumRegisteredTcpConnections];

void copy_mac(ethernet::MacAddress* destination, virtio_net::MacAddress source) {
    for(uint8_t index = 0; index < sizeof(destination->bytes); ++index) {
        destination->bytes[index] = source.bytes[index];
    }
}

uint32_t dhcp_transaction_id(ethernet::MacAddress mac) {
    return 0x4f536361U ^ (static_cast<uint32_t>(mac.bytes[2]) << 24U) ^ (static_cast<uint32_t>(mac.bytes[3]) << 16U) ^
           (static_cast<uint32_t>(mac.bytes[4]) << 8U) ^ mac.bytes[5];
}

bool send_dhcp_message(const uint8_t* payload, uint16_t payload_length, const ethernet::MacAddress& mac) {
    static uint8_t udp_packet[ethernet::kMaximumPayloadLength];
    static uint8_t ip_packet[ethernet::kMaximumPayloadLength];
    static uint8_t frame[kMaximumFrameLength];
    uint16_t udp_length = 0;
    uint16_t ip_length = 0;
    uint16_t frame_length = 0;
    ipv4::Interface bootstrap_interface;
    ipv4::initialize(&bootstrap_interface, nullptr, kZeroAddress, kZeroAddress, kZeroAddress);
    if(udp::build_packet(
           kZeroAddress, kBroadcastAddress, 68, 67, payload, payload_length, udp_packet, sizeof(udp_packet), &udp_length
       ) != udp::Status::Success ||
       ipv4::build_packet(
           bootstrap_interface,
           kBroadcastAddress,
           ipv4::Protocol::Udp,
           udp_packet,
           udp_length,
           ip_packet,
           sizeof(ip_packet),
           &ip_length
       ) != ipv4::FrameStatus::Success ||
       !ethernet::build_frame(
           frame, sizeof(frame), kBroadcast, mac, ethernet::EtherType::Ipv4, ip_packet, ip_length, &frame_length
       )) {
        return false;
    }
    return g_device->send(g_device->context, frame, frame_length) == virtio_net::Status::Success;
}

bool receive_dhcp_response(
    uint32_t transaction_id,
    const ethernet::MacAddress& mac,
    dhcp::MessageType expected_type,
    dhcp::Configuration* configuration
) {
    static uint8_t frame[kMaximumFrameLength];
    constexpr uint32_t kPollLimit = 1000000;
    for(uint32_t poll = 0; poll < kPollLimit; ++poll) {
        uint16_t length = 0;
        const virtio_net::Status receive_status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
        if(receive_status == virtio_net::Status::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(receive_status != virtio_net::Status::Success) {
            return false;
        }
        ethernet::FrameView ethernet_frame;
        if(!ethernet::parse_frame(frame, length, &ethernet_frame) || ethernet_frame.type != ethernet::EtherType::Ipv4 ||
           !ethernet::is_for_us(ethernet_frame, mac)) {
            continue;
        }
        ipv4::PacketView ip_packet;
        if(!ipv4::parse_packet(ethernet_frame.payload, ethernet_frame.payload_length, &ip_packet) ||
           ip_packet.protocol != ipv4::Protocol::Udp ||
           !arp::addresses_equal(ip_packet.destination, kBroadcastAddress)) {
            continue;
        }
        udp::DatagramView datagram;
        if(!udp::parse_packet(
               ip_packet.source, ip_packet.destination, ip_packet.payload, ip_packet.payload_length, &datagram
           ) ||
           datagram.source_port != 67 || datagram.destination_port != 68) {
            continue;
        }
        const dhcp::Status status = dhcp::parse_response(
            datagram.payload, datagram.payload_length, mac, transaction_id, expected_type, configuration
        );
        if(status == dhcp::Status::Success) {
            return true;
        }
    }
    return false;
}

bool acquire_dhcp(const ethernet::MacAddress& mac, dhcp::Configuration* configuration) {
    static uint8_t payload[576];
    uint16_t payload_length = 0;
    const uint32_t transaction_id = dhcp_transaction_id(mac);
    if(dhcp::build_discover(mac, transaction_id, payload, sizeof(payload), &payload_length) != dhcp::Status::Success ||
       !send_dhcp_message(payload, payload_length, mac)) {
        return false;
    }
    dhcp::Configuration offer;
    if(!receive_dhcp_response(transaction_id, mac, dhcp::MessageType::Offer, &offer)) {
        return false;
    }
    if(dhcp::build_request(
           mac, transaction_id, offer.address, offer.server_address, payload, sizeof(payload), &payload_length
       ) != dhcp::Status::Success ||
       !send_dhcp_message(payload, payload_length, mac)) {
        return false;
    }
    return receive_dhcp_response(transaction_id, mac, dhcp::MessageType::Ack, configuration);
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

bool same_tcp_endpoint(const tcp_connection::Connection& first, const tcp_connection::Connection& second) {
    return arp::addresses_equal(first.local_address, second.local_address) &&
           arp::addresses_equal(first.remote_address, second.remote_address) && first.local_port == second.local_port &&
           first.remote_port == second.remote_port;
}

bool register_tcp_connection(tcp_connection::Connection* connection) {
    if(connection == nullptr) {
        return false;
    }
    uint8_t free_slot = kMaximumRegisteredTcpConnections;
    for(uint8_t index = 0; index < kMaximumRegisteredTcpConnections; ++index) {
        if(!g_tcp_connections[index].active) {
            if(free_slot == kMaximumRegisteredTcpConnections) {
                free_slot = index;
            }
            continue;
        }
        if(g_tcp_connections[index].connection == connection ||
           same_tcp_endpoint(*g_tcp_connections[index].connection, *connection)) {
            return false;
        }
    }
    if(free_slot == kMaximumRegisteredTcpConnections) {
        return false;
    }
    g_tcp_connections[free_slot] = {.connection = connection, .active = true};
    return true;
}

void unregister_tcp_connection(tcp_connection::Connection* connection) {
    if(connection == nullptr) {
        return;
    }
    for(auto& entry : g_tcp_connections) {
        if(entry.active && entry.connection == connection) {
            entry = {};
            return;
        }
    }
}

bool is_registered_tcp_connection(const tcp_connection::Connection* connection) {
    if(connection == nullptr) {
        return false;
    }
    for(const auto& entry : g_tcp_connections) {
        if(entry.active && entry.connection == connection) {
            return true;
        }
    }
    return false;
}

enum class DispatchStatus : uint8_t {
    NotReady,
    Processed,
    IoError,
};

using Ipv4Handler = bool (*)(const ipv4::PacketView& packet, uint64_t now, void* context);

struct TcpDispatchContext {
    tcp_connection::Connection* waiting_connection;
    TcpStatus* result;
    bool matched;
};

bool dispatch_tcp_packet(const ipv4::PacketView& packet, uint64_t now, void* raw_context);

/**
 * Receive one frame and dispatch normal IPv4 traffic to the active operation.
 *
 * This is the first ownership boundary for network input: protocol operations
 * no longer access the VirtIO RX ring directly. DHCP remains a bootstrap
 * exception because it runs before the normal ARP/IPv4 interface exists.
 */
DispatchStatus dispatch_one(Ipv4Handler handler, void* context, TcpDispatchContext* tcp_context, uint64_t now) {
    static uint8_t frame[kMaximumFrameLength];
    uint16_t length = 0;
    const virtio_net::Status receive_status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
    if(receive_status == virtio_net::Status::NotReady) {
        return DispatchStatus::NotReady;
    }
    if(receive_status != virtio_net::Status::Success) {
        return DispatchStatus::IoError;
    }

    ethernet::FrameView ethernet_frame;
    if(!ethernet::parse_frame(frame, length, &ethernet_frame)) {
        return DispatchStatus::Processed;
    }
    if(ethernet_frame.type == ethernet::EtherType::Arp) {
        return process_incoming_arp(ethernet_frame, now) ? DispatchStatus::Processed : DispatchStatus::IoError;
    }
    if(ethernet_frame.type != ethernet::EtherType::Ipv4 ||
       !ethernet::is_for_us(ethernet_frame, g_arp_interface.hardware)) {
        return DispatchStatus::Processed;
    }

    ipv4::PacketView packet;
    if(!ipv4::parse_packet(ethernet_frame.payload, ethernet_frame.payload_length, &packet) ||
       !ipv4::is_for_us(g_ipv4_interface, packet)) {
        return DispatchStatus::Processed;
    }
    if(packet.protocol == ipv4::Protocol::Tcp) {
        (void)dispatch_tcp_packet(packet, now, tcp_context);
    } else if(handler != nullptr) {
        (void)handler(packet, now, context);
    }
    return DispatchStatus::Processed;
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

bool dispatch_tcp_packet(const ipv4::PacketView& ipv4_packet, uint64_t now, void* raw_context) {
    auto* context = static_cast<TcpDispatchContext*>(raw_context);
    if(ipv4_packet.protocol != ipv4::Protocol::Tcp) {
        return false;
    }
    tcp::SegmentView segment;
    if(!tcp::parse_segment(
           ipv4_packet.source, ipv4_packet.destination, ipv4_packet.payload, ipv4_packet.payload_length, &segment
       )) {
        return false;
    }

    bool matched = false;
    for(const auto& entry : g_tcp_connections) {
        if(!entry.active || !arp::addresses_equal(ipv4_packet.source, entry.connection->remote_address) ||
           !arp::addresses_equal(ipv4_packet.destination, entry.connection->local_address) ||
           segment.source_port != entry.connection->remote_port ||
           segment.destination_port != entry.connection->local_port) {
            continue;
        }
        matched = true;
        const tcp_connection::Result connection_result =
            tcp_connection::process(entry.connection, send_tcp_segment, nullptr, segment, now);
        if(context != nullptr && context->waiting_connection == entry.connection) {
            context->matched = true;
            if(connection_result == tcp_connection::Result::Reset) {
                *context->result = TcpStatus::Reset;
            } else if(connection_result == tcp_connection::Result::IoError) {
                *context->result = TcpStatus::IoError;
            }
        }
    }
    return matched;
}

bool process_tcp_input(tcp_connection::Connection* connection, uint64_t now, TcpStatus* result) {
    TcpDispatchContext context = {connection, result, false};
    const DispatchStatus status = dispatch_one(nullptr, nullptr, &context, now);
    if(status == DispatchStatus::NotReady) {
        asm volatile("pause");
        return false;
    }
    if(status == DispatchStatus::IoError) {
        *result = TcpStatus::IoError;
        return true;
    }
    return context.matched;
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

    while(time_remaining(start, timeout_ticks)) {
        const DispatchStatus status = dispatch_one(nullptr, nullptr, nullptr, timer::ticks());
        if(status == DispatchStatus::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(status == DispatchStatus::IoError) {
            return PingStatus::IoError;
        }
        if(arp::cache_lookup(g_arp_interface.cache, route.next_hop, timer::ticks(), &cached)) {
            return PingStatus::Success;
        }
    }
    return PingStatus::Timeout;
}

struct PingDispatchContext {
    arp::Ipv4Address destination;
    uint16_t identifier;
    uint16_t sequence;
    uint64_t start;
    uint64_t* elapsed_ticks;
    bool matched;
};

bool dispatch_ping_packet(const ipv4::PacketView& packet, uint64_t, void* raw_context) {
    auto* context = static_cast<PingDispatchContext*>(raw_context);
    if(packet.protocol != ipv4::Protocol::Icmp || !arp::addresses_equal(packet.source, context->destination)) {
        return false;
    }
    icmp::EchoView reply;
    if(!icmp::parse_echo(packet.payload, packet.payload_length, &reply) || reply.type != icmp::Type::EchoReply ||
       reply.identifier != context->identifier || reply.sequence != context->sequence) {
        return false;
    }
    *context->elapsed_ticks = timer::ticks() - context->start;
    context->matched = true;
    return true;
}

struct DnsDispatchContext {
    uint16_t source_port;
    uint16_t identifier;
    arp::Ipv4Address* address;
    ResolveStatus* result;
    bool matched;
};

bool dispatch_dns_packet(const ipv4::PacketView& packet, uint64_t, void* raw_context) {
    auto* context = static_cast<DnsDispatchContext*>(raw_context);
    if(packet.protocol != ipv4::Protocol::Udp || !arp::addresses_equal(packet.source, g_dns_server)) {
        return false;
    }
    udp::DatagramView datagram;
    if(!udp::parse_packet(g_dns_server, g_ipv4_interface.address, packet.payload, packet.payload_length, &datagram) ||
       datagram.source_port != 53 || datagram.destination_port != context->source_port) {
        return false;
    }
    const dns::Status result =
        dns::parse_response(datagram.payload, datagram.payload_length, context->identifier, context->address);
    if(result == dns::Status::Success) {
        *context->result = ResolveStatus::Success;
    } else if(result == dns::Status::NameNotFound) {
        *context->result = ResolveStatus::NameNotFound;
    } else if(result == dns::Status::ServerFailure || result == dns::Status::NoAddress) {
        *context->result = ResolveStatus::IoError;
    } else {
        return false;
    }
    context->matched = true;
    return true;
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
    dhcp::Configuration configuration;
    if(acquire_dhcp(mac, &configuration)) {
        arp::initialize(&g_arp_interface, mac, configuration.address);
        ipv4::initialize(
            &g_ipv4_interface, &g_arp_interface, configuration.address, configuration.netmask, configuration.gateway
        );
        g_dns_server = configuration.dns_server;
    } else {
        // A missing DHCP server should not prevent booting the development image;
        // retain the historical QEMU lease as a diagnostic fallback.
        arp::initialize(&g_arp_interface, mac, kFallbackAddress);
        ipv4::initialize(&g_ipv4_interface, &g_arp_interface, kFallbackAddress, kFallbackNetmask, kFallbackGateway);
        g_dns_server = kFallbackDnsServer;
    }
    g_initialized = true;
    return true;
}

bool is_initialized() {
    return g_initialized;
}

void tcp_unregister(tcp_connection::Connection* connection) {
    unregister_tcp_connection(connection);
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

    PingDispatchContext context = {destination, identifier, sequence, start, elapsed_ticks, false};
    while(time_remaining(start, timeout_ticks)) {
        const DispatchStatus status = dispatch_one(dispatch_ping_packet, &context, nullptr, timer::ticks());
        if(status == DispatchStatus::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(status == DispatchStatus::IoError) {
            return PingStatus::IoError;
        }
        if(context.matched) {
            return PingStatus::Success;
        }
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
    const PingStatus resolution = resolve(g_dns_server, timeout_ticks);
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
           g_dns_server,
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
    ResolveStatus result = ResolveStatus::Timeout;
    DnsDispatchContext context = {kDnsSourcePort, identifier, address, &result, false};
    while(time_remaining(start, timeout_ticks)) {
        const DispatchStatus status = dispatch_one(dispatch_dns_packet, &context, nullptr, timer::ticks());
        if(status == DispatchStatus::NotReady) {
            asm volatile("pause");
            continue;
        }
        if(status == DispatchStatus::IoError) {
            return ResolveStatus::IoError;
        }
        if(context.matched) {
            return result;
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
    if(connection == nullptr || destination_port == 0 || is_registered_tcp_connection(connection)) {
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
    if(!register_tcp_connection(connection)) {
        return TcpStatus::IoError;
    }
    g_tcp_sequence += 0x10001U;
    const uint64_t start = timer::ticks();
    if(tcp_connection::open(connection, send_tcp_segment, nullptr, start) != tcp_connection::Result::Success) {
        unregister_tcp_connection(connection);
        return TcpStatus::IoError;
    }
    while(time_remaining(start, timeout_ticks)) {
        const uint64_t now = timer::ticks();
        const tcp_connection::Result retry = tcp_connection::poll(connection, send_tcp_segment, nullptr, now);
        if(retry == tcp_connection::Result::Timeout) {
            unregister_tcp_connection(connection);
            return TcpStatus::Timeout;
        }
        TcpStatus input_result = TcpStatus::Success;
        (void)process_tcp_input(connection, now, &input_result);
        if(input_result != TcpStatus::Success) {
            unregister_tcp_connection(connection);
            return input_result;
        }
        if(connection->state == tcp_connection::State::Established) {
            return TcpStatus::Success;
        }
        if(connection->state == tcp_connection::State::Reset) {
            unregister_tcp_connection(connection);
            return TcpStatus::Reset;
        }
    }
    unregister_tcp_connection(connection);
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
        unregister_tcp_connection(connection);
        return TcpStatus::Success;
    }
    interrupts::enable();
    const uint64_t start = timer::ticks();
    if(connection->state == tcp_connection::State::Established &&
       tcp_connection::close(connection, send_tcp_segment, nullptr, start) != tcp_connection::Result::Success) {
        unregister_tcp_connection(connection);
        return TcpStatus::IoError;
    }
    while(time_remaining(start, timeout_ticks)) {
        if(connection->state == tcp_connection::State::Closed || connection->state == tcp_connection::State::TimeWait) {
            unregister_tcp_connection(connection);
            return TcpStatus::Success;
        }
        const uint64_t now = timer::ticks();
        if(tcp_connection::poll(connection, send_tcp_segment, nullptr, now) == tcp_connection::Result::Timeout) {
            unregister_tcp_connection(connection);
            return TcpStatus::Timeout;
        }
        TcpStatus input_result = TcpStatus::Success;
        (void)process_tcp_input(connection, now, &input_result);
        if(input_result == TcpStatus::Reset) {
            unregister_tcp_connection(connection);
            return TcpStatus::Reset;
        }
        if(input_result == TcpStatus::IoError) {
            unregister_tcp_connection(connection);
            return TcpStatus::IoError;
        }
    }
    unregister_tcp_connection(connection);
    return TcpStatus::Timeout;
}

} // namespace network

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
