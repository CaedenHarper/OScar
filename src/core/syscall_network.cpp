#include "arp.hpp"
#include "descriptor_table.hpp"
#include "kernel_heap.hpp"
#include "network.hpp"
#include "process_internal.hpp"
#include "syscall_internal.hpp"
#include "syscalls.hpp"
#include "tcp_connection.hpp"
#include "timer.hpp"
#include "user_memory.hpp"

#include <stdint.h>

namespace syscall_detail {

int64_t ping(const syscalls::Frame* frame) {
    if(frame->rdi == 0 || !user_memory::validate(frame->rdi, 4, false)) {
        return kErrorInvalidArgument;
    }
    uint8_t bytes[4];
    if(!user_memory::copy_from_user(&bytes[0], frame->rdi, sizeof(bytes))) {
        return kErrorInvalidArgument;
    }
    const arp::Ipv4Address destination = {{bytes[0], bytes[1], bytes[2], bytes[3]}};
    uint64_t elapsed_ticks = 0;
    const network::PingStatus result =
        network::ping(destination, frame->rsi, static_cast<uint16_t>(get_pid()), &elapsed_ticks);
    switch(result) {
        case network::PingStatus::Success: {
            const uint32_t frequency = timer::frequency_hz();
            return frequency == 0 ? kErrorIo
                                  : static_cast<int64_t>((elapsed_ticks * kMillisecondsPerSecond) / frequency);
        }
        case network::PingStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::PingStatus::InvalidArgument:
            return kErrorInvalidArgument;
        case network::PingStatus::AddressUnreachable:
            return kErrorAddressUnreachable;
        case network::PingStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::PingStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             hicpp-no-array-decay, cppcoreguidelines-pro-type-member-init)
bool copy_hostname(uintptr_t user_hostname, char* hostname) {
    if(user_hostname == 0 || hostname == nullptr) {
        return false;
    }
    for(uint64_t index = 0; index <= kMaximumHostnameLength; ++index) {
        if(user_hostname > UINTPTR_MAX - index ||
           !user_memory::copy_from_user(hostname + index, user_hostname + index, 1)) {
            return false;
        }
        if(hostname[index] == '\0') {
            return index != 0;
        }
    }
    return false;
}

int64_t resolve_hostname(const syscalls::Frame* frame) {
    char hostname[kMaximumHostnameLength + 1];
    if(frame->rdi == 0 || frame->rsi == 0 || !copy_hostname(frame->rdi, hostname) ||
       !user_memory::validate(frame->rsi, 4, true)) {
        return kErrorInvalidArgument;
    }
    arp::Ipv4Address address;
    const network::ResolveStatus result = network::resolve_hostname(hostname, frame->rdx, &address);
    switch(result) {
        case network::ResolveStatus::Success: {
            uint8_t bytes[4] = {address.bytes[0], address.bytes[1], address.bytes[2], address.bytes[3]};
            return user_memory::copy_to_user(frame->rsi, bytes, sizeof(bytes)) ? 0 : kErrorInvalidArgument;
        }
        case network::ResolveStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::ResolveStatus::InvalidArgument:
            return kErrorInvalidArgument;
        case network::ResolveStatus::NameNotFound:
            return kErrorNameNotFound;
        case network::ResolveStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::ResolveStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           hicpp-no-array-decay, cppcoreguidelines-pro-type-member-init)

int64_t socket_create(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || frame->rdi != kAddressFamilyIpv4 || frame->rsi != kSocketTypeStream ||
       (frame->rdx != 0 && frame->rdx != kProtocolTcp)) {
        return kErrorInvalidArgument;
    }
    auto* socket = static_cast<process::Socket*>(kernel_heap::allocate(sizeof(process::Socket)));
    if(socket == nullptr) {
        return kErrorIo;
    }
    tcp_connection::initialize(&socket->connection, {}, {}, 0, 0, 0);
    socket->references = 1;
    const int32_t descriptor = owner == nullptr ? -1 : descriptor::allocate_socket(&owner->descriptors, socket);
    if(descriptor < 0) {
        (void)kernel_heap::free(socket);
        return kErrorIo;
    }
    return descriptor;
}

int64_t socket_connect(const syscalls::Frame* frame) {
    auto* owner = current_process();
    auto* socket = owner == nullptr ? nullptr : descriptor::socket(&owner->descriptors, frame->rdi);
    if(socket == nullptr || frame->rsi == 0 || frame->rdx == 0 || frame->rdx > UINT16_MAX ||
       !user_memory::validate(frame->rsi, sizeof(arp::Ipv4Address), false)) {
        return kErrorInvalidArgument;
    }
    arp::Ipv4Address address = {};
    if(!user_memory::copy_from_user(&address, frame->rsi, sizeof(address))) {
        return kErrorInvalidArgument;
    }
    switch(network::tcp_connect(&socket->connection, address, static_cast<uint16_t>(frame->rdx), kSocketTimeoutTicks)) {
        case network::TcpStatus::Success:
            return 0;
        case network::TcpStatus::AddressUnreachable:
            return kErrorAddressUnreachable;
        case network::TcpStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::TcpStatus::Reset:
            return kErrorConnectionReset;
        case network::TcpStatus::NotConnected:
            return kErrorNotConnected;
        case network::TcpStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::TcpStatus::InvalidArgument:
        case network::TcpStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
}

int64_t socket_send(const syscalls::Frame* frame) {
    auto* owner = current_process();
    auto* socket = owner == nullptr ? nullptr : descriptor::socket(&owner->descriptors, frame->rdi);
    if(socket == nullptr || frame->rdx == 0 || frame->rdx > kMaximumSocketTransfer ||
       !user_memory::validate(frame->rsi, frame->rdx, false)) {
        return kErrorInvalidArgument;
    }
    uint8_t buffer[kMaximumSocketTransfer];
    if(!user_memory::copy_from_user(&buffer[0], frame->rsi, frame->rdx)) {
        return kErrorInvalidArgument;
    }
    switch(network::tcp_send(&socket->connection, &buffer[0], static_cast<uint16_t>(frame->rdx), kSocketTimeoutTicks)) {
        case network::TcpStatus::Success:
            return static_cast<int64_t>(frame->rdx);
        case network::TcpStatus::Reset:
            return kErrorConnectionReset;
        case network::TcpStatus::NotConnected:
            return kErrorNotConnected;
        case network::TcpStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::TcpStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::TcpStatus::InvalidArgument:
        case network::TcpStatus::AddressUnreachable:
        case network::TcpStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
}

int64_t socket_receive(const syscalls::Frame* frame) {
    auto* owner = current_process();
    auto* socket = owner == nullptr ? nullptr : descriptor::socket(&owner->descriptors, frame->rdi);
    if(socket == nullptr || frame->rdx == 0 || frame->rdx > kMaximumSocketTransfer ||
       !user_memory::validate(frame->rsi, frame->rdx, true)) {
        return kErrorInvalidArgument;
    }
    uint8_t buffer[kMaximumSocketTransfer];
    uint16_t received = 0;
    switch(network::tcp_receive(
        &socket->connection, &buffer[0], static_cast<uint16_t>(frame->rdx), &received, kSocketTimeoutTicks
    )) {
        case network::TcpStatus::Success:
            if(!user_memory::copy_to_user(frame->rsi, &buffer[0], received)) {
                return kErrorInvalidArgument;
            }
            return received;
        case network::TcpStatus::Reset:
            return kErrorConnectionReset;
        case network::TcpStatus::NotConnected:
            return kErrorNotConnected;
        case network::TcpStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::TcpStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::TcpStatus::InvalidArgument:
        case network::TcpStatus::AddressUnreachable:
        case network::TcpStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
}

} // namespace syscall_detail
