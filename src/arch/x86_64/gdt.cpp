#include "gdt.hpp"

#include <stdint.h>

namespace {

// Descriptor fields and bit positions below are fixed by the x86-64 architecture.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

struct [[gnu::packed]] DescriptorPointer {
    uint16_t limit;
    uint64_t base;
};

struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7]; // NOLINT(readability-magic-numbers) architectural TSS layout
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap;
};

static_assert(sizeof(TaskStateSegment) == 104); // NOLINT(readability-magic-numbers) architectural TSS size

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) CPU tables outlive initialization
uint64_t g_gdt[7];
TaskStateSegment g_tss = {};
bool g_initialized = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" void gdt_load(const DescriptorPointer* pointer);

uint64_t make_tss_descriptor(uintptr_t base) {
    constexpr uint64_t kTypeAvailableTss = 0x89ULL;
    constexpr uint64_t kLimit = sizeof(TaskStateSegment) - 1;
    return (kLimit & 0xffffULL) | ((base & 0xffffffULL) << 16U) | (kTypeAvailableTss << 40U) |
           (((kLimit >> 16U) & 0xfULL) << 48U) | (((base >> 24U) & 0xffULL) << 56U);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
} // namespace

namespace gdt {

bool initialize() {
    if(g_initialized) {
        return false;
    }

    // Long-mode code descriptors differ only by privilege level; data descriptors retain
    // a flat base and limit because paging, rather than segmentation, defines memory bounds.
    g_gdt[0] = 0;
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers) encoded descriptors
    g_gdt[1] = 0x00af9a000000ffffULL;
    g_gdt[2] = 0x00cf92000000ffffULL;
    g_gdt[3] = 0x00affa000000ffffULL;
    g_gdt[4] = 0x00cff2000000ffffULL;
    g_gdt[5] = make_tss_descriptor(reinterpret_cast<uintptr_t>(&g_tss));
    g_gdt[6] = reinterpret_cast<uintptr_t>(&g_tss) >> 32U;
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    g_tss.iomap = sizeof(TaskStateSegment);

    const DescriptorPointer pointer = {
        .limit = sizeof(g_gdt) - 1,
        .base = reinterpret_cast<uintptr_t>(&g_gdt),
    };
    gdt_load(&pointer);
    asm volatile("ltr %0" : : "r"(kTaskStateSelector) : "memory");
    g_initialized = true;
    return true;
}

void set_kernel_stack(uintptr_t stack_top) {
    g_tss.rsp0 = stack_top;
}

} // namespace gdt
