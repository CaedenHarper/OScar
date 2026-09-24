#include "idt.hpp"

#include "panic.hpp"
#include "serial.hpp"

#include <stdint.h>

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

IdtEntry g_idt[256];
uint16_t g_code_selector;

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

void set_gate(unsigned vector, uintptr_t address) {
    IdtEntry& entry = g_idt[vector];
    entry.offset_low = address & 0xffff;
    entry.selector = g_code_selector;
    entry.ist = 0;
    entry.attributes = 0x8e; // Present, ring 0, interrupt gate.
    entry.offset_middle = (address >> 16) & 0xffff;
    entry.offset_high = (address >> 32) & 0xffffffff;
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

    panic::halt("unhandled CPU exception");
}

namespace idt {

void initialize() {
    asm volatile("mov %%cs, %0" : "=r"(g_code_selector));

    for(unsigned vector = 0; vector < 32; ++vector) {
        set_gate(vector, isr_stub_table[vector]);
    }

    const Idtr idtr = {
        .limit = sizeof(g_idt) - 1,
        .base = reinterpret_cast<uintptr_t>(&g_idt),
    };
    asm volatile("lidt %0" : : "m"(idtr) : "memory");
    serial::write("IDT initialized.\n");
}

} // namespace idt
