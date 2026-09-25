#pragma once

namespace kernel_thread {
struct Thread;
} // namespace kernel_thread

namespace synchronization {

struct WaitQueue {
    kernel_thread::Thread* head;
    kernel_thread::Thread* tail;
};

/** Initialize an empty intrusive FIFO wait queue. */
void initialize(WaitQueue* queue);

/** Return whether the queue contains no blocked threads. */
bool empty(const WaitQueue* queue);

/**
 * Append a thread to a wait queue. Interrupts must be disabled and the thread must be
 * Running, not Ready or already present on another queue.
 */
bool enqueue_locked(WaitQueue* queue, kernel_thread::Thread* thread);

/** Remove and return the oldest waiter. Interrupts must be disabled. */
kernel_thread::Thread* dequeue_locked(WaitQueue* queue);

/** Remove a specific waiter. Interrupts must be disabled. */
bool remove_locked(WaitQueue* queue, kernel_thread::Thread* thread);

} // namespace synchronization
