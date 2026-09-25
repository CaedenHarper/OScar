#include "idt.hpp"

#include "panic.hpp"
#include "serial.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) this file uses hardware tables which require array
// indexing

namespace {

struct [[gnu::packed]] IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] Idtr {
    uint16_t limit;
    uint64_t base;
};

struct ExceptionFrame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

extern "C" uintptr_t isr_stub_table[];
extern "C" uintptr_t irq_stub_table[];
extern "C" uintptr_t syscall80;

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) these remain alive permanently and cannot be const
constexpr unsigned kIdtEntryCount = 256;
IdtEntry g_idt[kIdtEntryCount];
uint16_t g_code_selector;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr const char* kExceptionNames[] = {
    "Divide error",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception",
    "Reserved",
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) this function is private, low risk
void set_gate(unsigned vector, uintptr_t address, uint8_t descriptor_privilege_level) {
    constexpr uintptr_t kOffset16Mask = 0xffffU;
    constexpr uintptr_t kOffset32Mask = 0xffffffffU;

    constexpr unsigned kIdtPresent = 1U << 7U;
    constexpr unsigned kInterruptGateType = 0x0eU;
    constexpr auto kKernelInterruptGate = static_cast<uint8_t>(kIdtPresent | kInterruptGateType);

    constexpr unsigned kOffsetMiddleShift = 16;
    constexpr unsigned kOffsetHighShift = 32;

    IdtEntry& entry = g_idt[vector];
    entry.offset_low = address & kOffset16Mask;
    entry.selector = g_code_selector;
    entry.ist = 0;
    constexpr unsigned kDescriptorPrivilegeShift = 5U;
    entry.attributes =
        kKernelInterruptGate | static_cast<uint8_t>(descriptor_privilege_level << kDescriptorPrivilegeShift);
    entry.offset_middle = (address >> kOffsetMiddleShift) & kOffset16Mask;
    entry.offset_high = (address >> kOffsetHighShift) & kOffset32Mask;
    entry.reserved = 0;
}

void print_register(const char* name, uint64_t value) {
    serial::write("  ");
    serial::write(name);
    serial::write(" = ");
    serial::write_hex(value);
    serial::write("\n");
}

} // namespace

extern "C" [[noreturn]] void idt_exception_handler(ExceptionFrame* frame) {
    const uint64_t vector = frame->vector;
    serial::write("\nEXCEPTION: ");
    if(vector < sizeof(kExceptionNames) / sizeof(kExceptionNames[0])) {
        serial::write(kExceptionNames[vector]);
    } else {
        serial::write("Unknown exception");
    }
    serial::write(" (#");
    serial::write_u64(vector);
    serial::write(")\n");

    print_register("error", frame->error_code);
    print_register("rip", frame->rip);
    print_register("rsp", frame->rsp);
    print_register("rax", frame->rax);
    print_register("rbx", frame->rbx);
    print_register("rcx", frame->rcx);
    print_register("rdx", frame->rdx);
    print_register("rsi", frame->rsi);
    print_register("rdi", frame->rdi);
    print_register("rbp", frame->rbp);
    print_register("rflags", frame->rflags);

    // There is no recovery policy yet, and returning would re-execute the faulting
    // instruction with the same corrupted or invalid machine state.
    panic::halt("unhandled CPU exception");
}

namespace idt {

void initialize() {
    asm volatile("mov %%cs, %0" : "=r"(g_code_selector));

    // Install only the exceptions and timer currently understood by the kernel; leaving
    // unrelated vectors unconfigured avoids claiming ownership of future device IRQs.
    constexpr unsigned kExceptionVectorCount = 32;
    for(unsigned vector = 0; vector < kExceptionVectorCount; ++vector) {
        set_gate(vector, isr_stub_table[vector], 0);
    }

    constexpr unsigned kTimerVector = 32;
    set_gate(kTimerVector, irq_stub_table[0], 0);

    // Only the syscall vector is callable from ring 3; all hardware and exception gates
    // remain ring-0-only so user code cannot synthesize privileged interrupt paths.
    constexpr unsigned kSyscallVector = 0x80;
    set_gate(kSyscallVector, reinterpret_cast<uintptr_t>(&syscall80), 3);

    const Idtr idtr = {
        .limit = sizeof(g_idt) - 1,
        .base = reinterpret_cast<uintptr_t>(&g_idt),
    };
    asm volatile("lidt %0" : : "m"(idtr) : "memory");
    serial::write("IDT initialized.\n");
}

} // namespace idt

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
