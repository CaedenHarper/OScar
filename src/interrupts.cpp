#include "interrupts.hpp"

namespace interrupts {

void enable() {
    asm volatile("sti" : : : "memory");
}

void disable() {
    asm volatile("cli" : : : "memory");
}

} // namespace interrupts
