#include "interrupt_controller.hpp"

#include "io.hpp"
#include "serial.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace {

constexpr uint16_t kMasterCommandPort = 0x20;
constexpr uint16_t kMasterDataPort = 0x21;
constexpr uint16_t kSlaveCommandPort = 0xa0;
constexpr uint16_t kSlaveDataPort = 0xa1;
constexpr uint16_t kCommandInitialization = 0x11;
constexpr uint16_t kMode8086 = 0x01;
constexpr uint8_t kMasterVectorOffset = 32;
constexpr uint8_t kSlaveVectorOffset = 40;
constexpr uint8_t kMasterCascadeBit = 0x04;
constexpr uint8_t kSlaveCascadeIdentity = 0x02;
constexpr uint8_t kEndOfInterrupt = 0x20;
constexpr uint8_t kTimerIrqMask = 0xfe;
constexpr uint8_t kAllIrqsMasked = 0xff;
constexpr uint16_t kIoWaitPort = 0x80;
constexpr uint8_t kIoWaitValue = 0;
constexpr uint8_t kSlaveIrqBoundary = 8;
constexpr uintptr_t kIoApicPhysicalAddress = 0xfec00000;
constexpr uintptr_t kLocalApicIdOffset = 0x20;
constexpr uintptr_t kLocalApicEoiOffset = 0xb0;
constexpr uintptr_t kLocalApicSpuriousOffset = 0xf0;
constexpr uint8_t kIoApicVersionRegister = 1;
constexpr uint8_t kIoApicTimerRedirectionLow = 0x10;
constexpr uint8_t kIoApicTimerGsiOverride = 2;
constexpr uint8_t kIoApicRedirectionStride = 2;
constexpr uint8_t kTimerVector = 32;
constexpr uint32_t kInvalidMmioValue = 0xffffffff;
constexpr unsigned kLapicIdShift = 24U;
constexpr uint32_t kApicBaseMsr = 0x1b;
constexpr uint64_t kApicEnableBit = 1ULL << 11U;
constexpr uint32_t kSpuriousInterruptVector = 0xff;
constexpr uint32_t kSpuriousInterruptEnable = 1U << 8U;
constexpr uint32_t kSpuriousInterruptMask = 0xffffff00U;
constexpr uint32_t kApicAddressMask = 0xfffff000U;
constexpr uint32_t kByteMask = 0xffU;
constexpr unsigned kRegisterWordBits = 32U;

void io_wait() {
    io::out8(kIoWaitPort, kIoWaitValue);
}

// NOLINTBEGIN(hicpp-no-assembler) architectural MSR access is required to enable the LAPIC
uint64_t read_msr(uint32_t register_number) {
    // NOLINTBEGIN(misc-const-correctness) assembly writes these output operands
    uint32_t low = 0;
    uint32_t high = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(register_number));
    // NOLINTEND(misc-const-correctness)
    return (static_cast<uint64_t>(high) << kRegisterWordBits) | low;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void write_msr(uint32_t register_number, uint64_t value) {
    const auto low = static_cast<uint32_t>(value);
    const auto high = static_cast<uint32_t>(value >> kRegisterWordBits);
    asm volatile("wrmsr" : : "c"(register_number), "a"(low), "d"(high));
}
// NOLINTEND(hicpp-no-assembler)

// NOLINTBEGIN(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic) MMIO requires raw addresses
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) interrupt routing state must outlive initialization
volatile uint32_t* g_ioapic;
volatile uint32_t* g_lapic_eoi;
bool g_use_ioapic;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

volatile uint32_t* mmio(uintptr_t hhdm_offset, uintptr_t physical_address) {
    return reinterpret_cast<volatile uint32_t*>(hhdm_offset + physical_address);
}

bool map_mmio_page(uintptr_t hhdm_offset, uintptr_t physical_address) {
    const uintptr_t virtual_address = hhdm_offset + physical_address;
    if(virtual_memory::map_page(
           virtual_address, physical_address, virtual_memory::kWritable | virtual_memory::kNoExecute
       )) {
        return true;
    }

    uintptr_t translated_address = 0;
    return virtual_memory::translate(virtual_address, &translated_address) &&
           (translated_address & ~(virtual_memory::kPageSize - 1)) == physical_address;
}

uint32_t ioapic_read(uint8_t register_number) {
    g_ioapic[0] = register_number;
    return g_ioapic[4];
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ioapic_write(uint8_t register_number, uint32_t value) {
    g_ioapic[0] = register_number;
    g_ioapic[4] = value;
}
// NOLINTEND(performance-no-int-to-ptr, cppcoreguidelines-pro-bounds-pointer-arithmetic)

} // namespace

namespace interrupt_controller {

void initialize(uintptr_t hhdm_offset) {
    // Program the legacy PIC first and mask it while probing the APIC path. This leaves a
    // safe fallback if MMIO mapping or APIC discovery fails during early boot.
    const uint8_t master_mask = 0xff;
    const uint8_t slave_mask = 0xff;
    io::out8(kMasterDataPort, master_mask);
    io::out8(kSlaveDataPort, slave_mask);

    io::out8(kMasterCommandPort, kCommandInitialization);
    io_wait();
    io::out8(kSlaveCommandPort, kCommandInitialization);
    io_wait();

    io::out8(kMasterDataPort, kMasterVectorOffset);
    io_wait();
    io::out8(kSlaveDataPort, kSlaveVectorOffset);
    io_wait();

    io::out8(kMasterDataPort, kMasterCascadeBit);
    io_wait();
    io::out8(kSlaveDataPort, kSlaveCascadeIdentity);
    io_wait();

    io::out8(kMasterDataPort, kMode8086);
    io_wait();
    io::out8(kSlaveDataPort, kMode8086);
    io_wait();

    io::out8(kMasterDataPort, kTimerIrqMask);
    io::out8(kSlaveDataPort, kAllIrqsMasked);

    const uint64_t apic_base_msr = read_msr(kApicBaseMsr);
    write_msr(kApicBaseMsr, apic_base_msr | kApicEnableBit);
    const uintptr_t local_apic_physical_address = apic_base_msr & kApicAddressMask;
    if(!map_mmio_page(hhdm_offset, kIoApicPhysicalAddress) ||
       !map_mmio_page(hhdm_offset, local_apic_physical_address)) {
        g_use_ioapic = false;
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers) MMIO
    // registers
    volatile uint32_t* local_apic = mmio(hhdm_offset, local_apic_physical_address);
    g_ioapic = mmio(hhdm_offset, kIoApicPhysicalAddress);
    g_lapic_eoi = local_apic + (kLocalApicEoiOffset / sizeof(uint32_t));
    local_apic[kLocalApicSpuriousOffset / sizeof(uint32_t)] =
        (local_apic[kLocalApicSpuriousOffset / sizeof(uint32_t)] & kSpuriousInterruptMask) | kSpuriousInterruptEnable |
        kSpuriousInterruptVector;
    const uint32_t ioapic_version = ioapic_read(kIoApicVersionRegister);
    if(ioapic_version != 0 && ioapic_version != kInvalidMmioValue) {
        const uint32_t lapic_id = (local_apic[kLocalApicIdOffset / sizeof(uint32_t)] >> kLapicIdShift) & kByteMask;
        ioapic_write(kIoApicTimerRedirectionLow, kTimerVector);
        ioapic_write(kIoApicTimerRedirectionLow + 1, lapic_id << kLapicIdShift);
        const uint8_t override_redirection =
            kIoApicTimerRedirectionLow + (kIoApicTimerGsiOverride * kIoApicRedirectionStride);
        ioapic_write(override_redirection, kTimerVector);
        ioapic_write(override_redirection + 1, lapic_id << kLapicIdShift);
        g_use_ioapic = true;
        serial::write("Interrupt controller: IOAPIC.\n");
        return;
    }

    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-avoid-magic-numbers)

    // Keep the PIC configuration usable when the platform does not expose a valid IOAPIC
    // page or version register; timer delivery still works through the legacy route.
    g_use_ioapic = false;
    serial::write("Interrupt controller: PIC fallback.\n");
}

void end_of_interrupt(uint8_t irq) {
    // IRQs from the slave PIC require two acknowledgements. The LAPIC EOI is independent
    // of that cascade and is issued whenever APIC routing was selected.
    if(g_use_ioapic) {
        *g_lapic_eoi = 0;
    }

    if(irq >= kSlaveIrqBoundary) {
        io::out8(kSlaveCommandPort, kEndOfInterrupt);
    }
    io::out8(kMasterCommandPort, kEndOfInterrupt);
}

} // namespace interrupt_controller
