#include "interrupts.hpp"

#include "interrupt_controller.hpp"

#include <stdint.h>

namespace {

constexpr unsigned kInterruptEnableFlagBit = 9U;

struct Registration {
    interrupts::Handler handler;
    void* context;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) registrations live for the kernel lifetime.
Registration g_handlers[256] = {};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) allocation state lives for the kernel lifetime.
bool g_allocated_vectors[256] = {};

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

bool register_handler(uint8_t vector, Handler handler, void* context) {
    if(vector < kFirstExternalVector || vector > kLastExternalVector || handler == nullptr) {
        return false;
    }

    const State previous_state = save_and_disable();
    Registration& registration = g_handlers[vector];
    if(registration.handler != nullptr) {
        restore(previous_state);
        return false;
    }
    registration = {.handler = handler, .context = context};
    g_allocated_vectors[vector] = true;
    restore(previous_state);
    return true;
}

bool unregister_handler(uint8_t vector, Handler handler) {
    if(vector < kFirstExternalVector || vector > kLastExternalVector || handler == nullptr) {
        return false;
    }

    const State previous_state = save_and_disable();
    Registration& registration = g_handlers[vector];
    if(registration.handler != handler) {
        restore(previous_state);
        return false;
    }
    registration = {};
    g_allocated_vectors[vector] = false;
    restore(previous_state);
    return true;
}

bool allocate_vector(uint8_t* vector) {
    if(vector == nullptr) {
        return false;
    }

    const State previous_state = save_and_disable();
    for(uint16_t candidate = kFirstExternalVector; candidate <= kLastExternalVector; ++candidate) {
        if(!g_allocated_vectors[candidate]) {
            g_allocated_vectors[candidate] = true;
            *vector = static_cast<uint8_t>(candidate);
            restore(previous_state);
            return true;
        }
    }
    restore(previous_state);
    return false;
}

bool release_vector(uint8_t vector) {
    if(vector < kFirstExternalVector || vector > kLastExternalVector) {
        return false;
    }

    const State previous_state = save_and_disable();
    if(g_handlers[vector].handler != nullptr || !g_allocated_vectors[vector]) {
        restore(previous_state);
        return false;
    }
    g_allocated_vectors[vector] = false;
    restore(previous_state);
    return true;
}

void dispatch(uint8_t vector) {
    if(vector < kFirstExternalVector || vector > kLastExternalVector) {
        return;
    }
    const Registration registration = g_handlers[vector];
    if(registration.handler != nullptr) {
        registration.handler(vector, registration.context);
    }
}

} // namespace interrupts

extern "C" void interrupt_dispatch(uint64_t vector) {
    interrupts::dispatch(static_cast<uint8_t>(vector));
}

extern "C" void interrupt_end_of_interrupt(uint64_t vector) {
    interrupt_controller::end_of_interrupt(static_cast<uint8_t>(vector));
}
