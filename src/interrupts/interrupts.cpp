#include "interrupts.hpp"

namespace interrupts {

void enable() {
    // Keep interrupt state transitions explicit at scheduler call sites so queue mutation
    // and context selection can be treated as one critical section.
    asm volatile("sti" : : : "memory");
}

void disable() {
    asm volatile("cli" : : : "memory");
}

} // namespace interrupts
