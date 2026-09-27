#include "virtio_block.hpp"

#include "io.hpp"
#include "memory.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)

namespace virtio_block {

namespace {

constexpr uint16_t kPciConfigAddress = 0xcf8;
constexpr uint16_t kPciConfigData = 0xcfc;
constexpr uint32_t kPciConfigEnable = 0x80000000U;
constexpr uint16_t kVirtioVendor = 0x1af4;
constexpr uint16_t kVirtioLegacyBlock = 0x1001;
constexpr uint32_t kVirtqueuePages = 4;
constexpr uint64_t kBytesPerSector = 512;
constexpr uint64_t kPollLimit = 10000000;

constexpr uint8_t kStatusAcknowledge = 1;
constexpr uint8_t kStatusDriver = 2;
constexpr uint8_t kStatusDriverOk = 4;

constexpr uint16_t kDescriptorNext = 1;
constexpr uint16_t kDescriptorWrite = 2;

constexpr uint32_t kRequestRead = 0;
constexpr uint32_t kRequestWrite = 1;
constexpr uint32_t kRequestFlush = 4;
constexpr uint32_t kFeatureFlush = 1U << 9U;
constexpr uintptr_t kQueueAlignment = 4096;

struct Descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct RequestHeader {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct State {
    uint16_t io_base;
    uintptr_t queue_physical;
    uint16_t queue_size;
    block_device::Geometry geometry;
    block_device::Device device;
    Descriptor* descriptors;
    volatile uint16_t* available_index;
    uintptr_t used_ring_offset;
    volatile uint16_t* used_index;
    RequestHeader* request;
    uintptr_t request_physical;
    uint8_t* status;
    uint16_t last_used;
};

State g_state = {};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    const uint32_t address = kPciConfigEnable | (static_cast<uint32_t>(bus) << 16U) |
                             (static_cast<uint32_t>(slot) << 11U) | (static_cast<uint32_t>(function) << 8U) |
                             (offset & 0xfcU);
    io::out32(kPciConfigAddress, address);
    return io::in32(kPciConfigData);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    const uint32_t value = pci_read32(bus, slot, function, offset);
    return static_cast<uint16_t>((value >> ((offset & 2U) * 8U)) & 0xffffU);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value) {
    const uint32_t address = kPciConfigEnable | (static_cast<uint32_t>(bus) << 16U) |
                             (static_cast<uint32_t>(slot) << 11U) | (static_cast<uint32_t>(function) << 8U) |
                             (offset & 0xfcU);
    io::out32(kPciConfigAddress, address);
    uint32_t current = io::in32(kPciConfigData);
    const uint32_t shift = (offset & 2U) * 8U;
    current = (current & ~(0xffffU << shift)) | (static_cast<uint32_t>(value) << shift);
    io::out32(kPciConfigAddress, address);
    io::out32(kPciConfigData, current);
}

bool find_device(uint8_t* bus_out, uint8_t* slot_out, uint8_t* function_out, uint16_t* io_base_out) {
    for(uint16_t bus = 0; bus < 256; ++bus) {
        for(uint8_t slot = 0; slot < 32; ++slot) {
            for(uint8_t function = 0; function < 8; ++function) {
                const uint32_t identity = pci_read32(static_cast<uint8_t>(bus), slot, function, 0);
                if(static_cast<uint16_t>(identity) != kVirtioVendor ||
                   static_cast<uint16_t>(identity >> 16U) != kVirtioLegacyBlock) {
                    continue;
                }
                const uint32_t bar = pci_read32(static_cast<uint8_t>(bus), slot, function, 0x10);
                if((bar & 1U) == 0 || (bar & 0xfffffffcU) == 0) {
                    return false;
                }
                *bus_out = static_cast<uint8_t>(bus);
                *slot_out = slot;
                *function_out = function;
                *io_base_out = static_cast<uint16_t>(bar & 0xfffcU);
                return true;
            }
        }
    }
    return false;
}

bool translate_buffer(const void* buffer, uint64_t length, uintptr_t* physical) {
    if(buffer == nullptr || length == 0 || physical == nullptr ||
       !virtual_memory::translate(reinterpret_cast<uintptr_t>(buffer), physical)) {
        return false;
    }
    const uint64_t page_offset = *physical % virtual_memory::kPageSize;
    return page_offset + length <= virtual_memory::kPageSize;
}

block_device::Status request(uint32_t type, uint64_t first_block, uint32_t block_count, void* buffer, bool write) {
    const bool has_data = type != kRequestFlush;
    const uint64_t bytes = static_cast<uint64_t>(block_count) * g_state.geometry.block_size;
    uintptr_t buffer_physical = 0;
    if(has_data && (buffer == nullptr || !translate_buffer(buffer, bytes, &buffer_physical))) {
        return block_device::Status::InvalidArgument;
    }

    g_state.request->type = type;
    g_state.request->reserved = 0;
    g_state.request->sector = (first_block * g_state.geometry.block_size) / kBytesPerSector;
    *g_state.status = 0xff;

    g_state.descriptors[0] = {g_state.request_physical, sizeof(RequestHeader), kDescriptorNext, 1};
    if(has_data) {
        g_state.descriptors[1] = {
            buffer_physical,
            static_cast<uint32_t>(bytes),
            static_cast<uint16_t>(kDescriptorNext | (!write ? kDescriptorWrite : 0)),
            2,
        };
        g_state.descriptors[2] = {g_state.request_physical + sizeof(RequestHeader), 1, kDescriptorWrite, 0};
    } else {
        g_state.descriptors[1] = {g_state.request_physical + sizeof(RequestHeader), 1, kDescriptorWrite, 0};
    }

    auto* available_ring =
        reinterpret_cast<volatile uint16_t*>(reinterpret_cast<uint8_t*>(g_state.descriptors) + 16 * g_state.queue_size);
    const uint16_t available_index = *g_state.available_index;
    available_ring[2 + (available_index % g_state.queue_size)] = 0;
    *g_state.available_index = available_index + 1;
    asm volatile("" : : : "memory");
    io::out16(static_cast<uint16_t>(g_state.io_base + 16), 0);

    for(uint64_t poll = 0; poll < kPollLimit; ++poll) {
        if(*g_state.used_index != g_state.last_used) {
            ++g_state.last_used;
            return *g_state.status == 0 ? block_device::Status::Success : block_device::Status::IoError;
        }
        asm volatile("pause");
    }
    return block_device::Status::NotReady;
}

block_device::Status read_blocks(void*, uint64_t first_block, uint32_t block_count, void* buffer) {
    return request(kRequestRead, first_block, block_count, buffer, false);
}

block_device::Status write_blocks(void*, uint64_t first_block, uint32_t block_count, const void* buffer) {
    return request(kRequestWrite, first_block, block_count, const_cast<void*>(buffer), true);
}

block_device::Status flush_device(void*) {
    return request(kRequestFlush, 0, 0, nullptr, false);
}

} // namespace

bool initialize() {
    if(g_state.io_base != 0) {
        return true;
    }

    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;
    uint16_t io_base = 0;
    if(!find_device(&bus, &slot, &function, &io_base)) {
        return false;
    }

    uint16_t command = pci_read16(bus, slot, function, 4);
    pci_write16(bus, slot, function, 4, static_cast<uint16_t>(command | 0x5));
    io::out8(io_base + 18, 0);
    io::out8(io_base + 18, kStatusAcknowledge);
    io::out8(io_base + 18, kStatusAcknowledge | kStatusDriver);
    const uint32_t device_features = io::in32(io_base);
    io::out32(io_base + 4, device_features & kFeatureFlush);
    io::out16(io_base + 14, 0);
    const uint16_t queue_size = io::in16(io_base + 12);
    if(queue_size < 3) {
        return false;
    }

    uintptr_t queue_physical = 0;
    if(!physical_memory::allocate_contiguous_pages(kVirtqueuePages, &queue_physical)) {
        return false;
    }
    auto* queue = static_cast<uint8_t*>(virtual_memory::direct_map(queue_physical));
    for(uint64_t index = 0; index < kVirtqueuePages * virtual_memory::kPageSize; ++index) {
        queue[index] = 0;
    }

    g_state.io_base = io_base;
    g_state.queue_physical = queue_physical;
    g_state.queue_size = queue_size;
    const uintptr_t available_end = 16 * g_state.queue_size + 4 + 2 * g_state.queue_size;
    g_state.used_ring_offset = (available_end + kQueueAlignment - 1) & ~(kQueueAlignment - 1);
    const uintptr_t request_offset = g_state.used_ring_offset + 4 + 8 * g_state.queue_size;
    if(request_offset + sizeof(RequestHeader) + 1 > kVirtqueuePages * virtual_memory::kPageSize) {
        return false;
    }
    g_state.descriptors = reinterpret_cast<Descriptor*>(queue);
    g_state.available_index = reinterpret_cast<volatile uint16_t*>(queue + 16 * g_state.queue_size + 2);
    g_state.used_index = reinterpret_cast<volatile uint16_t*>(queue + g_state.used_ring_offset + 2);
    g_state.request = reinterpret_cast<RequestHeader*>(queue + request_offset);
    g_state.request_physical = queue_physical + request_offset;
    g_state.status = queue + request_offset + sizeof(RequestHeader);
    io::out16(io_base + 14, 0);
    io::out32(io_base + 8, static_cast<uint32_t>(queue_physical / virtual_memory::kPageSize));
    io::out8(io_base + 18, kStatusAcknowledge | kStatusDriver | kStatusDriverOk);

    const uint64_t capacity =
        static_cast<uint64_t>(io::in32(io_base + 20)) | (static_cast<uint64_t>(io::in32(io_base + 24)) << 32U);
    g_state.geometry = {capacity, kBytesPerSector, true};
    g_state.device = {
        &g_state,
        g_state.geometry,
        read_blocks,
        write_blocks,
        (device_features & kFeatureFlush) != 0 ? flush_device : nullptr,
    };
    g_state.last_used = 0;
    return true;
}

block_device::Device* device() {
    return g_state.io_base == 0 ? nullptr : &g_state.device;
}

} // namespace virtio_block

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
