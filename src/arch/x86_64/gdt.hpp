#pragma once

#include <stdint.h>

namespace gdt {

constexpr uint16_t kKernelCodeSelector = 0x08;
constexpr uint16_t kKernelDataSelector = 0x10;
constexpr uint16_t kUserCodeSelector = 0x1b;
constexpr uint16_t kUserDataSelector = 0x23;
constexpr uint16_t kTaskStateSelector = 0x28;

/** Install the kernel/user GDT and one 64-bit TSS. Returns false on repeat initialization. */
bool initialize();

/** Set the kernel stack used when an interrupt transitions from user mode. */
void set_kernel_stack(uintptr_t stack_top);

} // namespace gdt
