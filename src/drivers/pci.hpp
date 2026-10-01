#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pci {

constexpr uint8_t kBarCount = 6;

enum class BarType : uint8_t {
    None,
    Io,
    Memory32,
    Memory64,
};

struct Bar {
    BarType type;
    uint64_t base;
    uint64_t size;
    bool prefetchable;
};

struct Device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint16_t command;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    Bar bars[kBarCount];
};

/**
 * Enumerate PCI functions visible through configuration mechanism #1.
 * The caller owns the output array; the return value is the total number of
 * functions found, even when the array is too small to hold every record.
 */
size_t enumerate(Device* output, size_t capacity);

/**
 * Find the first function with the requested vendor and device identifiers.
 * Returns false when no matching function exists or output is null.
 */
bool find(uint16_t vendor_id, uint16_t device_id, Device* output);

/**
 * Enable memory-space, I/O-space, and bus-master access for a PCI function.
 * This preserves all other command-register bits and returns false for an
 * invalid function record.
 */
bool enable_bus_mastering(const Device& device);

} // namespace pci
