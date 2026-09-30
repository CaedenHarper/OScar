#include "context.hpp"

#include <stdint.h>

namespace context {

extern "C" void context_start();
extern "C" void context_start_user();

namespace {

constexpr uintptr_t kStackAlignmentMask = ~static_cast<uintptr_t>(0xf);
constexpr uint64_t kInitialRflags = 0x202;

} // namespace

bool initialize(CpuContext* context, uintptr_t stack_top, Entry entry, void* argument) {
    if(context == nullptr || stack_top == 0 || entry == nullptr) {
        return false;
    }

    context->rsp = 0;
    context->rip = 0;
    context->rflags = 0;
    context->rbx = 0;
    context->rbp = 0;
    context->r12 = 0;
    context->r13 = 0;
    context->r14 = 0;
    context->r15 = 0;
    // The assembly bootstrap performs a normal call into the entry point, so keep the
    // saved stack at an ABI-compatible boundary before that call pushes its return address.
    context->rsp = stack_top & kStackAlignmentMask;
    context->rip = reinterpret_cast<uintptr_t>(context_start);
    context->rflags = kInitialRflags;
    // The assembly stub has no C++-level argument slots; these callee-saved registers
    // carry the entry routine and its argument until the stub can invoke it.
    context->r12 = reinterpret_cast<uintptr_t>(entry);
    context->r13 = reinterpret_cast<uintptr_t>(argument);
    return true;
}

bool initialize_user(CpuContext* context, uintptr_t stack_top, uintptr_t user_entry, uintptr_t user_stack) {
    return initialize_user(context, stack_top, user_entry, user_stack, 0, 0);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
bool initialize_user(
    CpuContext* context,
    uintptr_t stack_top,
    uintptr_t user_entry,
    uintptr_t user_stack,
    uint64_t argument_count,
    uintptr_t argument_vector
) {
    constexpr uintptr_t kUserAddressLimit = 0x0000800000000000ULL;
    if(context == nullptr || stack_top == 0 || user_entry >= kUserAddressLimit || user_stack >= kUserAddressLimit ||
       user_entry == 0 || user_stack == 0) {
        return false;
    }

    context->rsp = stack_top & kStackAlignmentMask;
    context->rip = reinterpret_cast<uintptr_t>(context_start_user);
    context->rflags = kInitialRflags;
    context->rbx = 0;
    context->rbp = 0;
    context->r12 = user_entry;
    context->r13 = user_stack;
    context->r14 = argument_count;
    context->r15 = argument_vector;
    return true;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace context
