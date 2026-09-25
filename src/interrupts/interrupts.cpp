#include "interrupts.hpp"

#include <stdint.h>

namespace {

constexpr unsigned kInterruptEnableFlagBit = 9U;

} // namespace

namespace interrupts {

void enable() {
    // Keep interrupt state transitions explicit at scheduler call sites so queue mutation
    // and context selection can be treated as one critical section.
    asm volatile("sti" : : : "memory");
}

void disable() {
    asm volatile("cli" : : : "memory");
}

State save_and_disable() {
    // NOLINTNEXTLINE(misc-const-correctness) inline assembly writes this output operand
    uint64_t flags = 0;
    asm volatile("pushfq; pop %0" : "=r"(flags) : : "memory");
    disable();
    return {(flags & (1ULL << kInterruptEnableFlagBit)) != 0};
}

void restore(State state) {
    if(state.enabled) {
        enable();
    } else {
        disable();
    }
}

} // namespace interrupts
