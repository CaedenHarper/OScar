#pragma once

#include "thread.hpp"

#include <stdint.h>

namespace scheduler {

constexpr uint32_t kDefaultTimeSliceTicks = 5;

/**
 * Initialize the single-CPU scheduler and create its idle thread. The kernel
 * heap must be initialized first, and this function must be called once.
 * Returns false if initialization or idle-thread creation fails.
 */
bool initialize();

/**
 * Append a Ready thread to the round-robin queue. The scheduler does not take
 * ownership of the thread. Interrupts must be disabled by the caller.
 */
bool enqueue(kernel_thread::Thread* thread);

/**
 * Remove a queued thread from the ready queue. The thread remains allocated
 * and in State::Ready. Interrupts must be disabled by the caller.
 */
bool remove(kernel_thread::Thread* thread);

/**
 * Account for one timer tick and request a reschedule when the current
 * thread's time slice expires. Called from the timer interrupt with
 * interrupts already disabled; it does not switch contexts itself.
 */
void timer_tick();

/**
 * Return the currently running thread, or nullptr before start() is called.
 * This accessor does not change scheduler state.
 */
kernel_thread::Thread* current();

/**
 * Return the scheduler-owned idle thread after initialization, or nullptr if
 * initialization has not completed.
 */
kernel_thread::Thread* idle();

/**
 * Voluntarily give up the CPU. A runnable current thread is appended to the
 * ready queue and the next queued thread is resumed. Returns immediately if
 * there is no other runnable thread. Interrupts are disabled during queue and
 * context-switch bookkeeping.
 */
void yield();

/**
 * Start scheduling from the boot context. This function selects the first
 * ready thread or the idle thread and never returns.
 */
[[noreturn]] void start();

} // namespace scheduler
