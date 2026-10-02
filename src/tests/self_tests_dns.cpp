#include "arp.hpp"
#include "dns.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stdint.h>

// The raw packet below intentionally uses wire-format offsets and octets to
// make each malformed-response case explicit.
// NOLINTBEGIN(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers, misc-const-correctness,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace self_tests_detail {

namespace {} // namespace

void test_dns() {
    static uint8_t query[512];
    uint16_t query_length = 0;
    if(dns::build_query("Example.com", 0x1234, query, sizeof(query), &query_length) != dns::Status::Success ||
       query_length != 29 || query[0] != 0x12 || query[1] != 0x34 || query[2] != 1 || query[12] != 7 ||
       query[20] != 3 || query[24] != 0) {
        panic::halt("DNS smoke test could not build a query");
    }

    static uint8_t response[64] = {
        0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 'e',  'x',
        'a',  'm',  'p',  'l',  'e',  0x03, 'c',  'o',  'm',  0x00, 0x00, 0x01, 0x00, 0x01, 0xc0,
        0x0c, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x04, 192,  0,    2,    1,
    };
    arp::Ipv4Address address = {};
    if(dns::parse_response(response, 45, 0x1234, &address) != dns::Status::Success || address.bytes[0] != 192 ||
       address.bytes[1] != 0 || address.bytes[2] != 2 || address.bytes[3] != 1) {
        panic::halt("DNS smoke test could not parse an A response");
    }

    response[0] = 0;
    if(dns::parse_response(response, 45, 0x1234, &address) != dns::Status::Malformed) {
        panic::halt("DNS smoke test accepted a mismatched transaction");
    }
    response[0] = 0x12;
    response[29] = 0xc0;
    response[30] = 0x1d;
    if(dns::parse_response(response, 45, 0x1234, &address) != dns::Status::Malformed) {
        panic::halt("DNS smoke test accepted a cyclic name pointer");
    }

    static uint8_t too_small[12];
    uint16_t ignored_length = 0;
    if(dns::build_query("example.com", 1, too_small, sizeof(too_small), &ignored_length) !=
       dns::Status::BufferTooSmall) {
        panic::halt("DNS smoke test ignored a short query buffer");
    }
    serial::write("DNS query and response parsing smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(readability-magic-numbers, cppcoreguidelines-avoid-magic-numbers, misc-const-correctness,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic)
