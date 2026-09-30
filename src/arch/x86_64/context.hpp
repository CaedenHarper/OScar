#pragma once

#include <stddef.h>
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

constexpr size_t kContextWordSize = sizeof(uint64_t);
constexpr size_t kContextOffsetRsp = 0;
constexpr size_t kContextOffsetRip = kContextOffsetRsp + kContextWordSize;
constexpr size_t kContextOffsetRflags = kContextOffsetRip + kContextWordSize;
constexpr size_t kContextOffsetRbx = kContextOffsetRflags + kContextWordSize;
constexpr size_t kContextOffsetRbp = kContextOffsetRbx + kContextWordSize;
constexpr size_t kContextOffsetR12 = kContextOffsetRbp + kContextWordSize;
constexpr size_t kContextOffsetR13 = kContextOffsetR12 + kContextWordSize;
constexpr size_t kContextOffsetR14 = kContextOffsetR13 + kContextWordSize;
constexpr size_t kContextOffsetR15 = kContextOffsetR14 + kContextWordSize;
constexpr size_t kContextSize = kContextOffsetR15 + kContextWordSize;

static_assert(offsetof(CpuContext, rsp) == kContextOffsetRsp);
static_assert(offsetof(CpuContext, rip) == kContextOffsetRip);
static_assert(offsetof(CpuContext, rflags) == kContextOffsetRflags);
static_assert(offsetof(CpuContext, rbx) == kContextOffsetRbx);
static_assert(offsetof(CpuContext, rbp) == kContextOffsetRbp);
static_assert(offsetof(CpuContext, r12) == kContextOffsetR12);
static_assert(offsetof(CpuContext, r13) == kContextOffsetR13);
static_assert(offsetof(CpuContext, r14) == kContextOffsetR14);
static_assert(offsetof(CpuContext, r15) == kContextOffsetR15);
static_assert(sizeof(CpuContext) == kContextSize);

/**
 * Save the current kernel context and resume the supplied kernel context.
 * Both contexts must use the same kernel address space, and interrupts must
 * be disabled or otherwise controlled by the caller. This routine switches
 * general-purpose callee-saved registers, RSP, RIP, and RFLAGS only; it does
 * not switch CR3 or floating-point state. It returns when current is resumed.
 */
extern "C" void switch_context(CpuContext* current, const CpuContext* next);

/**
 * Prepare a context to begin executing an entry function on a kernel stack.
 * stack_top must point one byte past a valid stack region, be nonzero, and
 * provide enough space for the entry function. The entry function must not
 * return; this function returns false for invalid arguments.
 */
bool initialize(CpuContext* context, uintptr_t stack_top, Entry entry, void* argument);

/**
 * Prepare a kernel context that enters a mapped user instruction pointer with a mapped
 * user stack. The user entry receives argument_count and argument_vector in the C
 * entry registers. Both addresses must be canonical user addresses; this does not create mappings.
 */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool initialize_user(CpuContext* context, uintptr_t stack_top, uintptr_t user_entry, uintptr_t user_stack);

/** Prepare a user context with explicit C entry arguments in rdi and rsi. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool initialize_user(
    CpuContext* context,
    uintptr_t stack_top,
    uintptr_t user_entry,
    uintptr_t user_stack,
    uint64_t argument_count,
    uintptr_t argument_vector
);

} // namespace context
