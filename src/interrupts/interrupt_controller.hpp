#pragma once

#include <stdint.h>

namespace interrupt_controller {

/*
 * Initialize and remap the legacy 8259 programmable interrupt controller and
 * configure the available IOAPIC/LAPIC route. The IDT must be initialized
 * first, and maskable CPU interrupts must remain disabled until this function
 * and the selected device handlers are ready.
 */
void initialize(uintptr_t hhdm_offset);

/*
 * Signal completion of a hardware interrupt to the controller. The IRQ must
 * be in the legacy PIC range from zero through fifteen.
 */
void end_of_interrupt(uint8_t irq);

} // namespace interrupt_controller
