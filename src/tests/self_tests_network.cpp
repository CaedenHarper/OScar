#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "virtio_net.hpp"

#include <stdint.h>

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
