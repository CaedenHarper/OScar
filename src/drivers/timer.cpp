#include "timer.hpp"

#include "interrupt_controller.hpp"
#include "pit.hpp"
#include "scheduler.hpp"

#include <stdint.h>

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) interrupt state must outlive initialization
volatile uint64_t g_ticks = 0;
uint32_t g_frequency_hz = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

namespace timer {

bool initialize(uint32_t frequency_hz) {
    uint32_t configured_frequency_hz = 0;
    if(!pit::initialize(frequency_hz, &configured_frequency_hz)) {
        return false;
    }

    g_ticks = 0;
    g_frequency_hz = configured_frequency_hz;
    return true;
}

uint64_t ticks() {
    return g_ticks;
}

uint32_t frequency_hz() {
    return g_frequency_hz;
}

} // namespace timer

extern "C" void timer_irq_handler() {
    const uint64_t current_ticks = g_ticks;
    g_ticks = current_ticks + 1;
    // Let the scheduler observe the tick before acknowledging the controller; the IRQ
    // exit hook then performs any requested switch after the hardware is fully serviced.
    scheduler::timer_tick(g_ticks);
    interrupt_controller::end_of_interrupt(0);
}
