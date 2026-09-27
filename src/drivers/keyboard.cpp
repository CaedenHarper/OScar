#include "keyboard.hpp"

#include "interrupts.hpp"
#include "scheduler.hpp"
#include "spinlock.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
//             cppcoreguidelines-pro-bounds-constant-array-index)

namespace {

constexpr uint32_t kEventCapacity = 128;

keyboard::Event g_events[kEventCapacity];
uint32_t g_event_head = 0;
uint32_t g_event_tail = 0;
uint32_t g_event_count = 0;
bool g_available = false;
synchronization::Spinlock g_event_lock = {};
synchronization::WaitQueue g_waiters = {};

} // namespace

namespace keyboard {

bool initialize() {
    if(g_available) {
        return true;
    }

    synchronization::initialize(&g_event_lock);
    synchronization::initialize(&g_waiters);
    g_event_head = 0;
    g_event_tail = 0;
    g_event_count = 0;
    g_available = true;
    return true;
}

bool is_available() {
    return g_available;
}

bool poll(Event* event) {
    if(!g_available || event == nullptr) {
        return false;
    }

    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    if(g_event_count == 0) {
        synchronization::unlock(&g_event_lock, previous_state);
        return false;
    }
    *event = g_events[g_event_head];
    g_event_head = (g_event_head + 1) % kEventCapacity;
    --g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);
    return true;
}

bool read(Event* event) {
    if(!g_available || event == nullptr) {
        return false;
    }

    for(;;) {
        if(poll(event)) {
            return true;
        }

        const interrupts::State previous_state = interrupts::save_and_disable();
        if(poll(event)) {
            interrupts::restore(previous_state);
            return true;
        }
        // Interrupts stay disabled while the waiter is linked so an arriving key cannot
        // wake the thread before it is visible on the wait queue.
        const bool blocked = scheduler::block_current(&g_waiters);
        interrupts::restore(previous_state);
        if(!blocked) {
            return false;
        }
    }
}

uint32_t pending_events() {
    if(!g_available) {
        return 0;
    }

    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    const uint32_t count = g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);
    return count;
}

void submit_event(const Event& event) {
    if(!g_available) {
        return;
    }

    const interrupts::State previous_state = synchronization::lock(&g_event_lock);
    if(g_event_count == kEventCapacity) {
        // Preserve recent input and keep the IRQ path bounded if a consumer stalls.
        g_event_head = (g_event_head + 1) % kEventCapacity;
        --g_event_count;
    }
    g_events[g_event_tail] = event;
    g_event_tail = (g_event_tail + 1) % kEventCapacity;
    ++g_event_count;
    synchronization::unlock(&g_event_lock, previous_state);

    (void)scheduler::wake_one(&g_waiters);
}

} // namespace keyboard

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
//           cppcoreguidelines-pro-bounds-constant-array-index)
