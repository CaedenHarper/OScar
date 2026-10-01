#include "pci.hpp"

#include "io.hpp"

#include <stdint.h>

// PCI configuration mechanism #1 uses a 32-bit address port and a data port.
// Accesses are serialized by the single CPU in this kernel; a later SMP design
// will need to protect this pair of ports with a lock.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-magic-numbers,
//             bugprone-easily-swappable-parameters)
namespace pci {

namespace {

constexpr uint16_t kConfigAddress = 0xcf8;
constexpr uint16_t kConfigData = 0xcfc;
constexpr uint32_t kConfigEnable = 0x80000000U;
constexpr uint8_t kHeaderTypeMask = 0x7f;
constexpr uint8_t kMultifunctionBit = 0x80;
constexpr uint8_t kEndpointHeader = 0;
constexpr uint8_t kBarOffset = 0x10;
constexpr uint8_t kCommandOffset = 0x04;

void clear_bytes(void* destination, size_t length) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for(size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

void copy_bytes(void* destination, const void* source, size_t length) {
    auto* destination_bytes = static_cast<uint8_t*>(destination);
    const auto* source_bytes = static_cast<const uint8_t*>(source);
    for(size_t index = 0; index < length; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return kConfigEnable | (static_cast<uint32_t>(bus) << 16U) | (static_cast<uint32_t>(slot) << 11U) |
           (static_cast<uint32_t>(function) << 8U) | (offset & 0xfcU);
}

uint32_t read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    io::out32(kConfigAddress, config_address(bus, slot, function, offset));
    return io::in32(kConfigData);
}

uint16_t read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    const uint32_t value = read32(bus, slot, function, offset);
    return static_cast<uint16_t>(value >> ((offset & 2U) * 8U));
}

uint8_t read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    const uint32_t value = read32(bus, slot, function, offset);
    return static_cast<uint8_t>(value >> ((offset & 3U) * 8U));
}

void write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    io::out32(kConfigAddress, config_address(bus, slot, function, offset));
    io::out32(kConfigData, value);
}

bool valid_location(const Device& device) {
    return device.slot < 32 && device.function < 8;
}

Bar probe_bar(const Device& device, uint8_t index, uint32_t value) {
    Bar result = {};
    if(index >= kBarCount || value == 0xffffffffU || value == 0) {
        return result;
    }

    const uint8_t offset = static_cast<uint8_t>(kBarOffset + index * sizeof(uint32_t));
    if((value & 1U) != 0) {
        result.type = BarType::Io;
        result.base = value & 0xfffffffcU;
        return result;
    }

    result.prefetchable = (value & 8U) != 0;
    const uint8_t memory_type = static_cast<uint8_t>((value >> 1U) & 3U);
    if(memory_type == 0) {
        result.type = BarType::Memory32;
        result.base = value & 0xfffffff0U;
    } else if(memory_type == 2 && index + 1 < kBarCount) {
        const uint32_t high = read32(device.bus, device.slot, device.function, static_cast<uint8_t>(offset + 4));
        result.type = BarType::Memory64;
        result.base = (static_cast<uint64_t>(high) << 32U) | (value & 0xfffffff0U);
    } else {
        return Bar{};
    }
    return result;
}

Bar read_bar(const Device& device, uint8_t index) {
    const uint8_t offset = static_cast<uint8_t>(kBarOffset + index * sizeof(uint32_t));
    const uint32_t original_low = read32(device.bus, device.slot, device.function, offset);
    if(original_low == 0 || original_low == 0xffffffffU) {
        return Bar{};
    }

    const bool is_io = (original_low & 1U) != 0;
    const bool is_64 = !is_io && ((original_low >> 1U) & 3U) == 2;
    const uint32_t original_high =
        is_64 && index + 1 < kBarCount
            ? read32(device.bus, device.slot, device.function, static_cast<uint8_t>(offset + 4))
            : 0;
    write32(device.bus, device.slot, device.function, offset, 0xffffffffU);
    const uint32_t mask_low = read32(device.bus, device.slot, device.function, offset);
    uint32_t mask_high = 0;
    if(is_64 && index + 1 < kBarCount) {
        write32(device.bus, device.slot, device.function, static_cast<uint8_t>(offset + 4), 0xffffffffU);
        mask_high = read32(device.bus, device.slot, device.function, static_cast<uint8_t>(offset + 4));
    }
    write32(device.bus, device.slot, device.function, offset, original_low);
    if(is_64 && index + 1 < kBarCount) {
        write32(device.bus, device.slot, device.function, static_cast<uint8_t>(offset + 4), original_high);
    }

    Bar result = probe_bar(device, index, original_low);
    if(result.type == BarType::None) {
        return result;
    }
    if(result.type == BarType::Io) {
        const uint32_t io_mask = mask_low & 0xfffffffcU;
        if(io_mask == 0) {
            return Bar{};
        }
        result.size = static_cast<uint64_t>(~io_mask + 1U);
        return result;
    }

    const uint64_t memory_mask = (static_cast<uint64_t>(mask_high) << 32U) | (mask_low & 0xfffffff0U);
    if(memory_mask == 0) {
        return Bar{};
    }
    result.size = (~memory_mask) + 1;
    return result;
}

bool read_device(uint8_t bus, uint8_t slot, uint8_t function, Device* output) {
    const uint32_t identity = read32(bus, slot, function, 0);
    const uint16_t vendor_id = static_cast<uint16_t>(identity);
    if(vendor_id == 0xffffU) {
        return false;
    }

    Device device;
    clear_bytes(&device, sizeof(device));
    device.bus = bus;
    device.slot = slot;
    device.function = function;
    device.vendor_id = vendor_id;
    device.device_id = static_cast<uint16_t>(identity >> 16U);
    device.command = read16(bus, slot, function, kCommandOffset);
    device.revision = read8(bus, slot, function, 8);
    device.prog_if = read8(bus, slot, function, 9);
    device.subclass = read8(bus, slot, function, 10);
    device.class_code = read8(bus, slot, function, 11);
    device.header_type = read8(bus, slot, function, 14);
    device.interrupt_line = read8(bus, slot, function, 0x3c);
    device.interrupt_pin = read8(bus, slot, function, 0x3d);
    if((device.header_type & kHeaderTypeMask) == kEndpointHeader) {
        for(uint8_t index = 0; index < kBarCount; ++index) {
            device.bars[index] = read_bar(device, index);
            if(device.bars[index].type == BarType::Memory64) {
                ++index;
            }
        }
    }
    copy_bytes(output, &device, sizeof(device));
    return true;
}

} // namespace

size_t enumerate(Device* output, size_t capacity) {
    size_t total = 0;
    for(uint16_t bus = 0; bus < 256; ++bus) {
        for(uint8_t slot = 0; slot < 32; ++slot) {
            if(static_cast<uint16_t>(read32(static_cast<uint8_t>(bus), slot, 0, 0)) == 0xffffU) {
                continue;
            }
            const uint8_t function_count =
                (read8(static_cast<uint8_t>(bus), slot, 0, 14) & kMultifunctionBit) == 0 ? 1 : 8;
            for(uint8_t function = 0; function < function_count; ++function) {
                Device device;
                if(!read_device(static_cast<uint8_t>(bus), slot, function, &device)) {
                    continue;
                }
                if(output != nullptr && total < capacity) {
                    copy_bytes(&output[total], &device, sizeof(device));
                }
                ++total;
            }
        }
    }
    return total;
}

bool find(uint16_t vendor_id, uint16_t device_id, Device* output) {
    if(output == nullptr) {
        return false;
    }
    for(uint16_t bus = 0; bus < 256; ++bus) {
        for(uint8_t slot = 0; slot < 32; ++slot) {
            if(static_cast<uint16_t>(read32(static_cast<uint8_t>(bus), slot, 0, 0)) == 0xffffU) {
                continue;
            }
            const uint8_t function_count =
                (read8(static_cast<uint8_t>(bus), slot, 0, 14) & kMultifunctionBit) != 0 ? 8 : 1;
            for(uint8_t function = 0; function < function_count; ++function) {
                Device device;
                if(read_device(static_cast<uint8_t>(bus), slot, function, &device) && device.vendor_id == vendor_id &&
                   device.device_id == device_id) {
                    copy_bytes(output, &device, sizeof(device));
                    return true;
                }
            }
        }
    }
    return false;
}

bool enable_bus_mastering(const Device& device) {
    if(!valid_location(device)) {
        return false;
    }
    const uint32_t command_status = read32(device.bus, device.slot, device.function, kCommandOffset);
    write32(device.bus, device.slot, device.function, kCommandOffset, command_status | 0x0005U);
    return true;
}

} // namespace pci
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-magic-numbers,
//            bugprone-easily-swappable-parameters)
