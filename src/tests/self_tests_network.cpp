#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "virtio_net.hpp"

#include <stdint.h>

// This smoke test passes a raw Ethernet-sized buffer to the driver and checks
// the hardware-reported octets; those are deliberate protocol-level checks.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)

namespace self_tests_detail {

void test_virtio_network() {
    if(!virtio_net::initialize() || virtio_net::device() == nullptr) {
        panic::halt("VirtIO network driver could not be initialized");
    }
    auto* network = virtio_net::device();
    bool nonzero = false;
    for(const uint8_t byte : network->mac.bytes) {
        nonzero = nonzero || byte != 0;
    }
    if(!nonzero) {
        panic::halt("VirtIO network driver returned an empty MAC address");
    }
    static uint8_t frame[1514];
    uint16_t length = 0;
    if(network->receive(network->context, frame, sizeof(frame), &length) != virtio_net::Status::NotReady ||
       length != 0) {
        panic::halt("VirtIO network driver did not report an empty receive queue");
    }
    serial::write("VirtIO polling network-driver initialization smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
