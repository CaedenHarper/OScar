#pragma once

#include "thread.hpp"
#include "wait_queue.hpp"

namespace synchronization {

struct Mutex {
    bool locked;
    kernel_thread::Thread* owner;
    WaitQueue waiters;
};

/** Initialize an unlocked mutex and its FIFO waiter queue. */
void initialize(Mutex* mutex);

/** Acquire a mutex, blocking the current scheduler-managed thread if necessary. */
bool lock(Mutex* mutex);

/** Attempt to acquire a mutex without blocking. */
bool try_lock(Mutex* mutex);

/** Release a mutex and wake the oldest blocked waiter, if any. */
bool unlock(Mutex* mutex);

} // namespace synchronization
