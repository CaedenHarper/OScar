#include "panic.hpp"
#include "pci.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

#include <stddef.h>
#include <stdint.h>

namespace self_tests_detail {

namespace {

constexpr size_t kDeviceCapacity = 8;
constexpr uint16_t kVirtioVendor = 0x1af4;
constexpr uint16_t kVirtioBlockDevice = 0x1001;
constexpr uint16_t kVirtioNetworkDevice = 0x1000;
constexpr uint16_t kBusMasterCommandBits = 0x0005;

} // namespace

void test_pci() {
    static pci::Device devices[kDeviceCapacity];
    const size_t device_count = pci::enumerate(&devices[0], kDeviceCapacity);
    if(device_count == 0) {
        panic::halt("PCI discovery smoke test found no devices");
    }

    static pci::Device virtio;
    if(!pci::find(kVirtioVendor, kVirtioBlockDevice, &virtio) || virtio.bars[0].type != pci::BarType::Io ||
       virtio.bars[0].base == 0 || virtio.bars[0].size == 0 || virtio.bars[0].size > UINT16_MAX) {
        panic::halt("PCI discovery smoke test could not describe the VirtIO block BAR");
    }

    const uint16_t old_command = virtio.command;
    if(!pci::enable_bus_mastering(virtio)) {
        panic::halt("PCI discovery smoke test could not enable the VirtIO function");
    }
    static pci::Device enabled;
    if(!pci::find(kVirtioVendor, kVirtioBlockDevice, &enabled) ||
       (enabled.command & kBusMasterCommandBits) != kBusMasterCommandBits ||
       (enabled.command & static_cast<uint16_t>(~kBusMasterCommandBits)) !=
           (old_command & static_cast<uint16_t>(~kBusMasterCommandBits))) {
        panic::halt("PCI discovery smoke test changed unrelated command bits");
    }
    static pci::Device network;
    if(!pci::find(kVirtioVendor, kVirtioNetworkDevice, &network)) {
        panic::halt("PCI discovery smoke test could not find the VirtIO network device");
    }
    serial::write("PCI device-discovery smoke test passed.\n");
}

} // namespace self_tests_detail
