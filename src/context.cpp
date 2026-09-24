#include "context.hpp"

#include <stdint.h>

namespace context {

extern "C" void context_start();

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
    context->rsp = stack_top & kStackAlignmentMask;
    context->rip = reinterpret_cast<uintptr_t>(context_start);
    context->rflags = kInitialRflags;
    context->r12 = reinterpret_cast<uintptr_t>(entry);
    context->r13 = reinterpret_cast<uintptr_t>(argument);
    return true;
}

} // namespace context
