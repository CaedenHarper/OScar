#include "dhcp.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

// DHCP fixtures intentionally inspect protocol fields and malformed option bounds.
// NOLINTBEGIN(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)

namespace self_tests_detail {

void test_dhcp() {
    constexpr ethernet::MacAddress kMac = {{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
    constexpr arp::Ipv4Address kOffered = {{10, 0, 2, 15}};
    constexpr arp::Ipv4Address kServer = {{10, 0, 2, 2}};
    static uint8_t packet[512];
    uint16_t length = 0;
    if(dhcp::build_discover(kMac, 0x12345678, packet, sizeof(packet), &length) != dhcp::Status::Success ||
       length < dhcp::kMinimumPacketLength || packet[0] != 1 || packet[1] != 1 || packet[2] != 6) {
        panic::halt("DHCP smoke test could not build a discover message");
    }
    if(dhcp::build_request(kMac, 0x12345678, kOffered, kServer, packet, sizeof(packet), &length) !=
           dhcp::Status::Success ||
       packet[0] != 1) {
        panic::halt("DHCP smoke test could not build a request message");
    }

    static uint8_t response[512] = {};
    for(uint16_t index = 0; index < dhcp::kMinimumPacketLength; ++index) {
        response[index] = packet[index];
    }
    response[0] = 2;
    response[16] = kOffered.bytes[0];
    response[17] = kOffered.bytes[1];
    response[18] = kOffered.bytes[2];
    response[19] = kOffered.bytes[3];
    response[236] = 0x63;
    response[237] = 0x82;
    response[238] = 0x53;
    response[239] = 0x63;
    uint16_t offset = 240;
    const uint8_t message[] = {53, 1, 2};
    const uint8_t netmask[] = {1, 4, 255, 255, 255, 0};
    const uint8_t gateway[] = {3, 4, 10, 0, 2, 2};
    const uint8_t dns[] = {6, 4, 10, 0, 2, 3};
    const uint8_t lease[] = {51, 4, 0, 0, 0, 60};
    const uint8_t server[] = {54, 4, 10, 0, 2, 2};
    const uint8_t* options[] = {message, netmask, gateway, dns, lease, server};
    const uint8_t option_lengths[] = {
        sizeof(message), sizeof(netmask), sizeof(gateway), sizeof(dns), sizeof(lease), sizeof(server)
    };
    for(uint8_t option = 0; option < sizeof(options) / sizeof(options[0]); ++option) {
        for(uint8_t index = 0; index < option_lengths[option]; ++index) {
            response[offset++] = options[option][index];
        }
    }
    response[offset++] = 255;
    dhcp::Configuration configuration;
    if(dhcp::parse_response(response, offset, kMac, 0x12345678, dhcp::MessageType::Offer, &configuration) !=
           dhcp::Status::Success ||
       configuration.address.bytes[3] != 15 || configuration.gateway.bytes[3] != 2 ||
       configuration.dns_server.bytes[3] != 3 || configuration.lease_seconds != 60) {
        panic::halt("DHCP smoke test could not parse an offer");
    }
    response[offset - 1] = 2;
    if(dhcp::parse_response(response, offset, kMac, 0x12345678, dhcp::MessageType::Offer, &configuration) !=
       dhcp::Status::Malformed) {
        panic::halt("DHCP smoke test accepted an invalid option terminator");
    }
    serial::write("DHCP packet build and parse smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
