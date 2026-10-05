#include "network.hpp"

#include "dhcp.hpp"
#include "dns.hpp"
#include "ethernet.hpp"
#include "icmp.hpp"
#include "interrupts.hpp"
#include "network_requests.hpp"
#include "scheduler.hpp"
#include "spinlock.hpp"
#include "thread.hpp"
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
synchronization::Spinlock g_tcp_registry_lock;
synchronization::Spinlock g_tcp_transmit_lock;
synchronization::Spinlock g_receive_lock;
kernel_thread::Thread* g_service_thread = nullptr;
bool g_service_started = false;

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

tcp_connection::State connection_state(tcp_connection::Connection* connection) {
    const interrupts::State previous_state = synchronization::lock(&connection->state_lock);
    const tcp_connection::State result = connection->state;
    synchronization::unlock(&connection->state_lock, previous_state);
    return result;
}

bool connection_send_pending(tcp_connection::Connection* connection) {
    const interrupts::State previous_state = synchronization::lock(&connection->state_lock);
    const bool result = connection->send_unacknowledged != connection->send_next;
    synchronization::unlock(&connection->state_lock, previous_state);
    return result;
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
    const interrupts::State previous_state = synchronization::lock(&g_tcp_registry_lock);
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
            synchronization::unlock(&g_tcp_registry_lock, previous_state);
            return false;
        }
    }
    if(free_slot == kMaximumRegisteredTcpConnections) {
        synchronization::unlock(&g_tcp_registry_lock, previous_state);
        return false;
    }
    g_tcp_connections[free_slot].connection = connection;
    g_tcp_connections[free_slot].active = true;
    synchronization::unlock(&g_tcp_registry_lock, previous_state);
    return true;
}

void unregister_tcp_connection(tcp_connection::Connection* connection) {
    if(connection == nullptr) {
        return;
    }
    const interrupts::State previous_state = synchronization::lock(&g_tcp_registry_lock);
    for(auto& entry : g_tcp_connections) {
        if(entry.active && entry.connection == connection) {
            entry.connection = nullptr;
            entry.active = false;
            synchronization::unlock(&g_tcp_registry_lock, previous_state);
            return;
        }
    }
    synchronization::unlock(&g_tcp_registry_lock, previous_state);
}

RegisteredTcpConnection* find_registered_tcp_connection(tcp_connection::Connection* connection) {
    const interrupts::State previous_state = synchronization::lock(&g_tcp_registry_lock);
    for(auto& entry : g_tcp_connections) {
        if(entry.active && entry.connection == connection) {
            synchronization::unlock(&g_tcp_registry_lock, previous_state);
            return &entry;
        }
    }
    synchronization::unlock(&g_tcp_registry_lock, previous_state);
    return nullptr;
}

bool has_registered_tcp_connections() {
    const interrupts::State previous_state = synchronization::lock(&g_tcp_registry_lock);
    for(auto& entry : g_tcp_connections) {
        if(entry.active) {
            synchronization::unlock(&g_tcp_registry_lock, previous_state);
            return true;
        }
    }
    synchronization::unlock(&g_tcp_registry_lock, previous_state);
    return false;
}

bool is_registered_tcp_connection(const tcp_connection::Connection* connection) {
    if(connection == nullptr) {
        return false;
    }
    const interrupts::State previous_state = synchronization::lock(&g_tcp_registry_lock);
    for(const auto& entry : g_tcp_connections) {
        if(entry.active && entry.connection == connection) {
            synchronization::unlock(&g_tcp_registry_lock, previous_state);
            return true;
        }
    }
    synchronization::unlock(&g_tcp_registry_lock, previous_state);
    return false;
}

enum class DispatchStatus : uint8_t {
    NotReady,
    Processed,
    IoError,
};

using Ipv4Handler = bool (*)(const ipv4::PacketView& packet, uint64_t now, void* context);

void dispatch_tcp_packet(const ipv4::PacketView& packet, uint64_t now);

/**
 * Receive one frame and dispatch normal IPv4 traffic to the active operation.
 *
 * This is the first ownership boundary for network input: protocol operations
 * no longer access the VirtIO RX ring directly. DHCP remains a bootstrap
 * exception because it runs before the normal ARP/IPv4 interface exists.
 */
DispatchStatus dispatch_one(Ipv4Handler handler, void* context, uint64_t now) {
    uint8_t frame[kMaximumFrameLength];
    uint16_t length = 0;
    const interrupts::State previous_state = synchronization::lock(&g_receive_lock);
    const virtio_net::Status receive_status = g_device->receive(g_device->context, frame, sizeof(frame), &length);
    synchronization::unlock(&g_receive_lock, previous_state);
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
        dispatch_tcp_packet(packet, now);
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
    const interrupts::State previous_state = synchronization::lock(&g_tcp_transmit_lock);
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
        synchronization::unlock(&g_tcp_transmit_lock, previous_state);
        return false;
    }
    static uint8_t frame[kMaximumFrameLength];
    uint16_t frame_length = 0;
    const bool sent = ipv4::build_frame(
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
    synchronization::unlock(&g_tcp_transmit_lock, previous_state);
    return sent;
}

void dispatch_tcp_packet(const ipv4::PacketView& ipv4_packet, uint64_t now) {
    if(ipv4_packet.protocol != ipv4::Protocol::Tcp) {
        return;
    }
    tcp::SegmentView segment;
    if(!tcp::parse_segment(
           ipv4_packet.source, ipv4_packet.destination, ipv4_packet.payload, ipv4_packet.payload_length, &segment
       )) {
        return;
    }

    const interrupts::State registry_state = synchronization::lock(&g_tcp_registry_lock);
    for(auto& entry : g_tcp_connections) {
        if(!entry.active || !arp::addresses_equal(ipv4_packet.source, entry.connection->remote_address) ||
           !arp::addresses_equal(ipv4_packet.destination, entry.connection->local_address) ||
           segment.source_port != entry.connection->remote_port ||
           segment.destination_port != entry.connection->local_port) {
            continue;
        }
        (void)tcp_connection::process(entry.connection, send_tcp_segment, nullptr, segment, now);
    }
    synchronization::unlock(&g_tcp_registry_lock, registry_state);
}

void poll_registered_connections(uint64_t now) {
    const interrupts::State registry_state = synchronization::lock(&g_tcp_registry_lock);
    for(auto& entry : g_tcp_connections) {
        if(!entry.active) {
            continue;
        }
        (void)tcp_connection::poll(entry.connection, send_tcp_segment, nullptr, now);
    }
    synchronization::unlock(&g_tcp_registry_lock, registry_state);
}

PingStatus resolve(arp::Ipv4Address destination, uint64_t timeout_ticks);

TcpStatus tcp_status_from_ping(PingStatus status) {
    switch(status) {
        case PingStatus::Success:
            return TcpStatus::Success;
        case PingStatus::Timeout:
            return TcpStatus::Timeout;
        case PingStatus::AddressUnreachable:
            return TcpStatus::AddressUnreachable;
        case PingStatus::NotInitialized:
            return TcpStatus::NotInitialized;
        case PingStatus::InvalidArgument:
        case PingStatus::IoError:
            return TcpStatus::IoError;
    }
    return TcpStatus::IoError;
}

void complete_request(network_requests::Request* request, TcpStatus status, uint16_t transferred = 0) {
    network_requests::complete(request, network_requests::State::Completed, static_cast<int32_t>(status), transferred);
    network_requests::release(request);
}

void cancel_request(network_requests::Request* request, TcpStatus status) {
    network_requests::cancel(request, static_cast<int32_t>(status));
    network_requests::release(request);
}

void unregister_request_connection(network_requests::Request* request) {
    if(request->registered) {
        unregister_tcp_connection(request->connection);
        request->registered = false;
    }
}

void process_request(network_requests::Request* request, uint64_t now) {
    if(request == nullptr) {
        return;
    }
    if(request->state != network_requests::State::Queued) {
        network_requests::release(request);
        return;
    }
    if(now >= request->deadline) {
        if((request->type == network_requests::Type::Connect || request->type == network_requests::Type::Close) &&
           request->registered) {
            unregister_request_connection(request);
        }
        cancel_request(request, TcpStatus::Timeout);
        return;
    }

    if(request->type == network_requests::Type::Connect) {
        if(!request->started) {
            const uint64_t remaining = request->deadline - now;
            const TcpStatus resolution = tcp_status_from_ping(resolve(request->destination, remaining));
            if(resolution != TcpStatus::Success) {
                complete_request(request, resolution);
                return;
            }
            const uint16_t local_port = g_next_tcp_port++;
            if(g_next_tcp_port == 0) {
                g_next_tcp_port = kFirstTcpEphemeralPort;
            }
            tcp_connection::initialize(
                request->connection,
                g_ipv4_interface.address,
                request->destination,
                local_port,
                request->destination_port,
                g_tcp_sequence
            );
            g_tcp_sequence += 0x10001U;
            if(!register_tcp_connection(request->connection) ||
               tcp_connection::open(request->connection, send_tcp_segment, nullptr, now) !=
                   tcp_connection::Result::Success) {
                unregister_tcp_connection(request->connection);
                complete_request(request, TcpStatus::IoError);
                return;
            }
            request->started = true;
            request->registered = true;
        }
        const tcp_connection::State state = connection_state(request->connection);
        if(state == tcp_connection::State::Established) {
            complete_request(request, TcpStatus::Success);
        } else if(state == tcp_connection::State::Reset || state == tcp_connection::State::Closed) {
            unregister_tcp_connection(request->connection);
            request->registered = false;
            complete_request(request, state == tcp_connection::State::Reset ? TcpStatus::Reset : TcpStatus::IoError);
        } else {
            (void)network_requests::enqueue(request);
        }
        return;
    }

    if(request->type == network_requests::Type::Close) {
        const tcp_connection::State state = connection_state(request->connection);
        if(state == tcp_connection::State::Closed) {
            unregister_request_connection(request);
            complete_request(request, TcpStatus::Success);
        } else if(state == tcp_connection::State::Reset) {
            unregister_request_connection(request);
            complete_request(request, TcpStatus::Reset);
        } else if(state == tcp_connection::State::TimeWait) {
            unregister_request_connection(request);
            complete_request(request, TcpStatus::Success);
        } else if(state == tcp_connection::State::Established) {
            const tcp_connection::Result result =
                tcp_connection::close(request->connection, send_tcp_segment, nullptr, now);
            if(result == tcp_connection::Result::Success || result == tcp_connection::Result::WouldBlock) {
                (void)network_requests::enqueue(request);
            } else if(result == tcp_connection::Result::Reset) {
                unregister_request_connection(request);
                complete_request(request, TcpStatus::Reset);
            } else {
                unregister_request_connection(request);
                complete_request(request, TcpStatus::IoError);
            }
        } else {
            (void)network_requests::enqueue(request);
        }
        return;
    }

    const tcp_connection::State state = connection_state(request->connection);
    if(state == tcp_connection::State::Reset) {
        complete_request(request, TcpStatus::Reset);
        return;
    }
    if(request->type == network_requests::Type::Send) {
        if(state != tcp_connection::State::Established) {
            complete_request(request, TcpStatus::NotConnected);
            return;
        }
        if(request->offset == request->requested && !connection_send_pending(request->connection)) {
            complete_request(request, TcpStatus::Success, request->requested);
            return;
        }
        if(!connection_send_pending(request->connection)) {
            const uint16_t remaining = request->requested - request->offset;
            const uint16_t chunk = remaining > kMaximumTcpPayload ? kMaximumTcpPayload : remaining;
            const tcp_connection::Result result = tcp_connection::send(
                request->connection, send_tcp_segment, nullptr, request->buffer + request->offset, chunk, now
            );
            if(result == tcp_connection::Result::Success) {
                request->offset = static_cast<uint16_t>(request->offset + chunk);
            } else if(result == tcp_connection::Result::Reset) {
                complete_request(request, TcpStatus::Reset);
                return;
            } else if(result != tcp_connection::Result::WouldBlock) {
                complete_request(request, TcpStatus::IoError);
                return;
            }
        }
        (void)network_requests::enqueue(request);
        return;
    }

    if(state != tcp_connection::State::Established && state != tcp_connection::State::FinWait &&
       state != tcp_connection::State::TimeWait) {
        complete_request(request, TcpStatus::NotConnected);
        return;
    }
    uint16_t received = 0;
    const tcp_connection::Result result =
        tcp_connection::receive(request->connection, request->buffer, request->requested, &received);
    if(result == tcp_connection::Result::Success || result == tcp_connection::Result::Closed) {
        complete_request(request, TcpStatus::Success, received);
    } else if(result == tcp_connection::Result::Reset) {
        complete_request(request, TcpStatus::Reset);
    } else {
        (void)network_requests::enqueue(request);
    }
}

void network_service_entry(void*) {
    for(;;) {
        interrupts::enable();
        const bool has_connections = has_registered_tcp_connections();
        if(has_connections) {
            constexpr uint8_t kMaximumFramesPerPass = 32;
            for(uint8_t index = 0; index < kMaximumFramesPerPass; ++index) {
                const DispatchStatus status = dispatch_one(nullptr, nullptr, timer::ticks());
                if(status == DispatchStatus::NotReady || status == DispatchStatus::IoError) {
                    break;
                }
            }
            poll_registered_connections(timer::ticks());
        }
        constexpr uint8_t kMaximumRequestsPerPass = 16;
        for(uint8_t index = 0; index < kMaximumRequestsPerPass; ++index) {
            auto* request = network_requests::dequeue();
            if(request == nullptr) {
                break;
            }
            process_request(request, timer::ticks());
        }
        if(!has_registered_tcp_connections() && !network_requests::has_pending()) {
            network_requests::wait_for_work();
        } else {
            scheduler::sleep(1);
        }
    }
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
        const DispatchStatus status = dispatch_one(nullptr, nullptr, timer::ticks());
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
    synchronization::initialize(&g_receive_lock);
    synchronization::initialize(&g_tcp_registry_lock);
    synchronization::initialize(&g_tcp_transmit_lock);
    network_requests::initialize();
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

bool start_service() {
    if(!g_initialized) {
        return false;
    }
    if(g_service_started) {
        return true;
    }
    g_service_thread = kernel_thread::create(network_service_entry, nullptr);
    if(g_service_thread == nullptr || !scheduler::enqueue(g_service_thread)) {
        if(g_service_thread != nullptr) {
            (void)kernel_thread::destroy(g_service_thread);
            g_service_thread = nullptr;
        }
        return false;
    }
    g_service_started = true;
    return true;
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
        const DispatchStatus status = dispatch_one(dispatch_ping_packet, &context, timer::ticks());
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
        const DispatchStatus status = dispatch_one(dispatch_dns_packet, &context, timer::ticks());
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
    auto* request = network_requests::allocate(network_requests::Type::Connect);
    if(request == nullptr) {
        return TcpStatus::IoError;
    }
    request->connection = connection;
    request->destination = destination;
    request->destination_port = destination_port;
    request->deadline = timer::ticks() + timeout_ticks;
    interrupts::enable();
    if(!network_requests::enqueue(request)) {
        network_requests::release(request);
        network_requests::release(request);
        return TcpStatus::IoError;
    }
    network_requests::wait(request);
    const auto result = static_cast<TcpStatus>(request->result);
    network_requests::release(request);
    return result;
}

TcpStatus tcp_send(tcp_connection::Connection* connection, const void* data, uint16_t length, uint64_t timeout_ticks) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr || data == nullptr || length == 0) {
        return TcpStatus::InvalidArgument;
    }
    auto* request = network_requests::allocate(network_requests::Type::Send);
    if(request == nullptr || length > sizeof(request->buffer)) {
        if(request != nullptr) {
            network_requests::release(request);
            network_requests::release(request);
        }
        return request == nullptr ? TcpStatus::IoError : TcpStatus::InvalidArgument;
    }
    request->connection = connection;
    request->requested = length;
    request->deadline = timer::ticks() + timeout_ticks;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for(uint16_t index = 0; index < length; ++index) {
        request->buffer[index] = bytes[index];
    }
    interrupts::enable();
    if(!network_requests::enqueue(request)) {
        network_requests::release(request);
        network_requests::release(request);
        return TcpStatus::IoError;
    }
    network_requests::wait(request);
    const auto result = static_cast<TcpStatus>(request->result);
    network_requests::release(request);
    return result;
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
    if(find_registered_tcp_connection(connection) == nullptr) {
        return TcpStatus::NotConnected;
    }
    auto* request = network_requests::allocate(network_requests::Type::Receive);
    if(request == nullptr) {
        return TcpStatus::IoError;
    }
    request->connection = connection;
    request->requested = capacity;
    request->deadline = timer::ticks() + timeout_ticks;
    interrupts::enable();
    if(!network_requests::enqueue(request)) {
        network_requests::release(request);
        network_requests::release(request);
        return TcpStatus::IoError;
    }
    network_requests::wait(request);
    const auto result = static_cast<TcpStatus>(request->result);
    *length = request->transferred;
    if(result == TcpStatus::Success && request->transferred != 0) {
        auto* bytes = static_cast<uint8_t*>(output);
        for(uint16_t index = 0; index < request->transferred; ++index) {
            bytes[index] = request->buffer[index];
        }
    }
    network_requests::release(request);
    return result;
}

TcpStatus tcp_close(tcp_connection::Connection* connection, uint64_t timeout_ticks) {
    if(!g_initialized || g_device == nullptr) {
        return TcpStatus::NotInitialized;
    }
    if(connection == nullptr) {
        return TcpStatus::InvalidArgument;
    }
    // Syscall entry clears IF. Every blocking-capable socket operation restores
    // an interruptible execution path before returning to user mode, including
    // this immediate cleanup path.
    interrupts::enable();
    // Closing an unregistered connection is already complete. This is the normal
    // cleanup path after a failed connect and must not create a request that waits
    // for a connection the service thread will never poll.
    if(!is_registered_tcp_connection(connection)) {
        return TcpStatus::Success;
    }
    auto* request = network_requests::allocate(network_requests::Type::Close);
    if(request == nullptr) {
        return TcpStatus::IoError;
    }
    request->connection = connection;
    request->registered = is_registered_tcp_connection(connection);
    request->deadline = timer::ticks() + timeout_ticks;
    if(!network_requests::enqueue(request)) {
        network_requests::release(request);
        network_requests::release(request);
        return TcpStatus::IoError;
    }
    network_requests::wait(request);
    const auto result = static_cast<TcpStatus>(request->result);
    network_requests::release(request);
    return result;
}

} // namespace network

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
