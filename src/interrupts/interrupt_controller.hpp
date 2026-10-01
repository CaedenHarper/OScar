#pragma once

#include <stdint.h>

namespace interrupt_controller {

constexpr uint8_t kTimerVector = 32;
constexpr uint8_t kKeyboardVector = 33;
constexpr uint8_t kSerialVector = 36;

/*
 * Initialize and remap the legacy 8259 programmable interrupt controller and
 * configure the available IOAPIC/LAPIC route. The IDT must be initialized
 * first, and maskable CPU interrupts must remain disabled until this function
 * and the selected device handlers are ready.
 */
void initialize(uintptr_t hhdm_offset);

/** Route a legacy IRQ line to an external interrupt vector. */
bool route_irq(uint8_t irq, uint8_t vector);

/** Mask one legacy IRQ line until its device handler is ready. */
bool mask_irq(uint8_t irq);

/** Unmask one previously routed legacy IRQ line. */
bool unmask_irq(uint8_t irq);

/*
 * Signal completion of a hardware interrupt identified by its IDT vector.
 */
void end_of_interrupt(uint8_t vector);

} // namespace interrupt_controller
