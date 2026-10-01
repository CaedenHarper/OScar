#include "virtio_net.hpp"

#include "io.hpp"
#include "memory.hpp"
#include "pci.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             bugprone-easily-swappable-parameters, readability-magic-numbers)
namespace virtio_net {

namespace {

constexpr uint16_t kVirtioVendor = 0x1af4;
constexpr uint16_t kVirtioNetworkDevice = 0x1000;
constexpr uint16_t kQueueEntries = 8;
// The descriptor and available rings occupy the first page; the legacy
// queue-alignment rule places the used ring at the second page boundary.
constexpr uint64_t kQueuePages = 2;
constexpr uint16_t kQueueAlignment = 4096;
constexpr uint16_t kMaximumFrameLength = 1514;
constexpr uint16_t kVirtioHeaderLength = 10;
constexpr uint64_t kPollLimit = 10000000;
constexpr uint8_t kStatusAcknowledge = 1;
constexpr uint8_t kStatusDriver = 2;
constexpr uint8_t kStatusDriverOk = 4;
constexpr uint16_t kDescriptorWrite = 2;

struct Descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct Queue {
    uintptr_t physical;
    Descriptor* descriptors;
    volatile uint16_t* available_index;
    volatile uint16_t* used_index;
    uintptr_t used_offset;
    uint16_t last_used;
};

struct State {
    uint16_t io_base;
    Queue rx;
    Queue tx;
    uintptr_t rx_buffer_physical[kQueueEntries];
    uint8_t* rx_buffers[kQueueEntries];
    uintptr_t tx_buffer_physical;
    uint8_t* tx_buffer;
    MacAddress mac;
    Device device;
};

State g_state = {};

void copy_bytes(void* destination, const void* source, uint64_t length) {
    auto* destination_bytes = static_cast<uint8_t*>(destination);
    const auto* source_bytes = static_cast<const uint8_t*>(source);
    for(uint64_t index = 0; index < length; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
}

void clear_bytes(void* destination, uint64_t length) {
    auto* bytes = static_cast<uint8_t*>(destination);
    for(uint64_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

bool initialize_queue(uint16_t index, Queue* queue) {
    io::out16(static_cast<uint16_t>(g_state.io_base + 14), index);
    if(io::in16(static_cast<uint16_t>(g_state.io_base + 12)) < kQueueEntries) {
        return false;
    }
    uintptr_t physical = 0;
    if(!physical_memory::allocate_contiguous_pages(kQueuePages, &physical)) {
        return false;
    }
    auto* memory = static_cast<uint8_t*>(virtual_memory::direct_map(physical));
    clear_bytes(memory, kQueuePages * virtual_memory::kPageSize);
    queue->physical = physical;
    queue->descriptors = reinterpret_cast<Descriptor*>(memory);
    queue->available_index = reinterpret_cast<volatile uint16_t*>(memory + 16 * kQueueEntries + 2);
    queue->used_offset = kQueueAlignment;
    queue->used_index = reinterpret_cast<volatile uint16_t*>(memory + queue->used_offset + 2);
    queue->last_used = 0;
    io::out32(static_cast<uint16_t>(g_state.io_base + 8), static_cast<uint32_t>(physical / kQueueAlignment));
    return true;
}

Status send_frame(void*, const void* frame, uint16_t length) {
    if(frame == nullptr || length == 0 || length > kMaximumFrameLength) {
        return Status::InvalidArgument;
    }
    clear_bytes(g_state.tx_buffer, kVirtioHeaderLength);
    copy_bytes(g_state.tx_buffer + kVirtioHeaderLength, frame, length);
    Queue& queue = g_state.tx;
    queue.descriptors[0] = {
        g_state.tx_buffer_physical,
        static_cast<uint32_t>(kVirtioHeaderLength + length),
        0,
        0,
    };
    const uint16_t available = *queue.available_index;
    auto* ring =
        reinterpret_cast<volatile uint16_t*>(reinterpret_cast<uint8_t*>(queue.descriptors) + 16 * kQueueEntries);
    ring[2 + (available % kQueueEntries)] = 0;
    *queue.available_index = available + 1;
    asm volatile("" : : : "memory");
    io::out16(static_cast<uint16_t>(g_state.io_base + 16), 1);
    for(uint64_t poll = 0; poll < kPollLimit; ++poll) {
        if(*queue.used_index != queue.last_used) {
            ++queue.last_used;
            return Status::Success;
        }
        asm volatile("pause");
    }
    return Status::NotReady;
}

Status receive_frame(void*, void* frame, uint16_t capacity, uint16_t* length) {
    if(frame == nullptr || length == nullptr || capacity == 0) {
        return Status::InvalidArgument;
    }
    Queue& queue = g_state.rx;
    if(*queue.used_index == queue.last_used) {
        return Status::NotReady;
    }
    auto* used_ring = reinterpret_cast<volatile uint8_t*>(queue.descriptors) + queue.used_offset + 4;
    const uint32_t used_length =
        *reinterpret_cast<volatile uint32_t*>(used_ring + 8 * (queue.last_used % kQueueEntries) + 4);
    const uint16_t descriptor =
        static_cast<uint16_t>(*reinterpret_cast<volatile uint32_t*>(used_ring + 8 * (queue.last_used % kQueueEntries)));
    ++queue.last_used;
    if(descriptor >= kQueueEntries || used_length < kVirtioHeaderLength) {
        return Status::IoError;
    }
    const uint16_t frame_length = static_cast<uint16_t>(used_length - kVirtioHeaderLength);
    const uint16_t copied = frame_length < capacity ? frame_length : capacity;
    copy_bytes(frame, g_state.rx_buffers[descriptor] + kVirtioHeaderLength, copied);
    *length = copied;
    const uint16_t available = *queue.available_index;
    auto* available_ring =
        reinterpret_cast<volatile uint16_t*>(reinterpret_cast<uint8_t*>(queue.descriptors) + 16 * kQueueEntries);
    available_ring[2 + (available % kQueueEntries)] = descriptor;
    *queue.available_index = available + 1;
    asm volatile("" : : : "memory");
    io::out16(static_cast<uint16_t>(g_state.io_base + 16), 0);
    return frame_length <= capacity ? Status::Success : Status::InvalidArgument;
}

} // namespace

bool initialize() {
    if(g_state.io_base != 0) {
        return true;
    }
    pci::Device pci_device;
    if(!pci::find(kVirtioVendor, kVirtioNetworkDevice, &pci_device) || pci_device.bars[0].type != pci::BarType::Io ||
       pci_device.bars[0].base == 0 || pci_device.bars[0].base > UINT16_MAX || !pci::enable_bus_mastering(pci_device)) {
        return false;
    }
    g_state.io_base = static_cast<uint16_t>(pci_device.bars[0].base);
    io::out8(static_cast<uint16_t>(g_state.io_base + 18), 0);
    io::out8(static_cast<uint16_t>(g_state.io_base + 18), kStatusAcknowledge);
    io::out8(static_cast<uint16_t>(g_state.io_base + 18), kStatusAcknowledge | kStatusDriver);
    for(uint8_t index = 0; index < sizeof(g_state.mac.bytes); ++index) {
        g_state.mac.bytes[index] = io::in8(static_cast<uint16_t>(g_state.io_base + 20 + index));
    }
    io::out32(static_cast<uint16_t>(g_state.io_base + 4), 0);
    if(!initialize_queue(0, &g_state.rx) || !initialize_queue(1, &g_state.tx)) {
        return false;
    }
    for(uint16_t index = 0; index < kQueueEntries; ++index) {
        if(!physical_memory::allocate_page(&g_state.rx_buffer_physical[index])) {
            return false;
        }
        g_state.rx_buffers[index] =
            static_cast<uint8_t*>(virtual_memory::direct_map(g_state.rx_buffer_physical[index]));
        clear_bytes(g_state.rx_buffers[index], virtual_memory::kPageSize);
        g_state.rx.descriptors[index] = {
            g_state.rx_buffer_physical[index], virtual_memory::kPageSize, kDescriptorWrite, 0
        };
        auto* available = reinterpret_cast<volatile uint16_t*>(
            reinterpret_cast<uint8_t*>(g_state.rx.descriptors) + 16 * kQueueEntries
        );
        available[2 + index] = index;
    }
    if(!physical_memory::allocate_page(&g_state.tx_buffer_physical)) {
        return false;
    }
    g_state.tx_buffer = static_cast<uint8_t*>(virtual_memory::direct_map(g_state.tx_buffer_physical));
    clear_bytes(g_state.tx_buffer, virtual_memory::kPageSize);
    *g_state.rx.available_index = kQueueEntries;
    io::out8(static_cast<uint16_t>(g_state.io_base + 18), kStatusAcknowledge | kStatusDriver | kStatusDriverOk);
    g_state.device = {&g_state, g_state.mac, send_frame, receive_frame};
    return true;
}

Device* device() {
    return g_state.io_base == 0 ? nullptr : &g_state.device;
}

} // namespace virtio_net

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           bugprone-easily-swappable-parameters, readability-magic-numbers)
