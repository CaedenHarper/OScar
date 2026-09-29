#pragma once

#include "thread.hpp"
#include "wait_queue.hpp"

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
 * Block the running thread on a FIFO wait queue and switch to another thread. Interrupts
 * must already be disabled; they remain disabled when this function returns after wake-up.
 */
bool block_current(synchronization::WaitQueue* queue);

/** Wake the oldest thread on a wait queue and append it to the ready queue. */
bool wake_one(synchronization::WaitQueue* queue);

/** Wake every thread currently on a wait queue and return the number released. */
uint32_t wake_all(synchronization::WaitQueue* queue);

/**
 * Put the current thread into State::Waiting until wake_tick. A deadline at or before
 * the current timer tick returns immediately; interrupts are disabled while scheduler
 * queues are updated. The current thread must be scheduler-managed.
 */
void sleep_until(uint64_t wake_tick);

/**
 * Put the current thread into State::Waiting for the requested number of timer ticks.
 * sleep(0) is defined as yield(), and therefore returns after another runnable thread
 * may have executed. The current thread must be scheduler-managed.
 */
void sleep(uint64_t ticks);

/**
 * Account for one timer tick and request a reschedule when the current
 * thread's time slice expires. Called from the timer interrupt with
 * interrupts already disabled; it wakes expired threads but does not switch contexts
 * itself.
 */
void timer_tick(uint64_t current_tick);

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

/**
 * Run the threads queued for the boot smoke-test phase and return after they
 * have all terminated. The scheduler must be initialized and its boot-phase
 * threads must already be queued; no user startup process should be queued yet.
 */
void start_bootstrap();

/** Terminate the current thread with an exit status and switch permanently to the next runnable thread. */
[[noreturn]] void thread_exit(kernel_thread::Thread* thread, int64_t status);

} // namespace scheduler
