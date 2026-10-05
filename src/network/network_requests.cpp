#include "network_requests.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "scheduler.hpp"
#include "spinlock.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace network_requests {

namespace {

Request* g_head = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
Request* g_tail = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
synchronization::Spinlock g_lock; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
synchronization::WaitQueue g_service_waiters; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
bool g_service_waiting = false; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

bool finished(State state) {
    return state == State::Completed || state == State::Cancelled;
}

} // namespace

void initialize() {
    synchronization::initialize(&g_lock);
    synchronization::initialize(&g_service_waiters);
    g_head = nullptr;
    g_tail = nullptr;
    g_service_waiting = false;
}

Request* allocate(Type type) {
    auto* request = static_cast<Request*>(kernel_heap::allocate(sizeof(Request)));
    if(request == nullptr) {
        return nullptr;
    }
    request->next = nullptr;
    request->type = type;
    request->state = State::Queued;
    request->connection = nullptr;
    request->destination = {};
    request->destination_port = 0;
    request->deadline = 0;
    request->requested = 0;
    request->transferred = 0;
    request->offset = 0;
    request->result = 0;
    request->started = false;
    request->registered = false;
    request->references = 2;
    synchronization::initialize(&request->completion_waiters);
    return request;
}

void retain(Request* request) {
    if(request == nullptr) {
        return;
    }
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    ++request->references;
    synchronization::unlock(&g_lock, previous_state);
}

void release(Request* request) {
    if(request == nullptr) {
        return;
    }
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    if(request->references == 0) {
        synchronization::unlock(&g_lock, previous_state);
        return;
    }
    --request->references;
    const bool reclaim = request->references == 0;
    synchronization::unlock(&g_lock, previous_state);
    if(reclaim) {
        (void)kernel_heap::free(request);
    }
}

bool enqueue(Request* request) {
    if(request == nullptr) {
        return false;
    }
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    if(finished(request->state)) {
        synchronization::unlock(&g_lock, previous_state);
        return false;
    }
    request->next = nullptr;
    if(g_tail == nullptr) {
        g_head = request;
        g_tail = request;
    } else {
        g_tail->next = request;
        g_tail = request;
    }
    if(g_service_waiting) {
        (void)scheduler::wake_one(&g_service_waiters);
    }
    synchronization::unlock(&g_lock, previous_state);
    return true;
}

Request* dequeue() {
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    auto* request = g_head;
    if(request != nullptr) {
        g_head = request->next;
        if(g_head == nullptr) {
            g_tail = nullptr;
        }
        request->next = nullptr;
    }
    synchronization::unlock(&g_lock, previous_state);
    return request;
}

bool has_pending() {
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    const bool pending = g_head != nullptr;
    synchronization::unlock(&g_lock, previous_state);
    return pending;
}

// The result and transfer count intentionally use different widths because the
// former carries a status code while the latter is a bounded byte count.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void complete(Request* request, State state, int32_t result, uint16_t transferred) {
    if(request == nullptr || !finished(state)) {
        return;
    }
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    if(!finished(request->state)) {
        request->state = state;
        request->result = result;
        request->transferred = transferred;
        (void)scheduler::wake_all(&request->completion_waiters);
    }
    synchronization::unlock(&g_lock, previous_state);
}

void cancel(Request* request, int32_t result) {
    if(request == nullptr) {
        return;
    }
    const interrupts::State previous_state = synchronization::lock(&g_lock);
    if(request->state == State::Queued) {
        request->state = State::Cancelled;
        request->result = result;
        request->transferred = 0;
        (void)scheduler::wake_all(&request->completion_waiters);
    }
    synchronization::unlock(&g_lock, previous_state);
}

void wait(Request* request) {
    if(request == nullptr) {
        return;
    }
    for(;;) {
        const interrupts::State previous_state = interrupts::save_and_disable();
        const interrupts::State lock_state = synchronization::lock(&g_lock);
        if(finished(request->state)) {
            synchronization::unlock(&g_lock, lock_state);
            interrupts::restore(previous_state);
            return;
        }
        synchronization::unlock(&g_lock, lock_state);
        // Interrupts remain disabled between the state check and enqueueing the
        // caller. This closes the completion-versus-sleep race on the single CPU.
        if(!scheduler::block_current(&request->completion_waiters)) {
            interrupts::restore(previous_state);
            return;
        }
        interrupts::restore(previous_state);
    }
}

void wait_for_work() {
    interrupts::disable();
    const interrupts::State lock_state = synchronization::lock(&g_lock);
    if(g_head != nullptr) {
        synchronization::unlock(&g_lock, lock_state);
        interrupts::enable();
        return;
    }
    g_service_waiting = true;
    synchronization::unlock(&g_lock, lock_state);
    (void)scheduler::block_current(&g_service_waiters);
    const interrupts::State wake_state = synchronization::lock(&g_lock);
    g_service_waiting = false;
    synchronization::unlock(&g_lock, wake_state);
    interrupts::enable();
}

} // namespace network_requests
