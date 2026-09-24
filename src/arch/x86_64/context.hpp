#pragma once

#include <stdint.h>

namespace context {

using Entry = void (*)(void*);

struct CpuContext {
    uintptr_t rsp;
    uintptr_t rip;
    uint64_t rflags;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

/*
 * Save the current kernel context and resume the supplied kernel context.
 * Both contexts must use the same kernel address space, and interrupts must
 * be disabled or otherwise controlled by the caller. This function returns
 * when the saved context is resumed.
 */
extern "C" void switch_context(CpuContext* current, const CpuContext* next);

/*
 * Prepare a context to begin executing an entry function on a kernel stack.
 * stack_top must point one byte past a valid stack region, be nonzero, and
 * provide enough space for the entry function. The entry function must not
 * return; this function returns false for invalid arguments.
 */
bool initialize(CpuContext* context, uintptr_t stack_top, Entry entry, void* argument);

} // namespace context
