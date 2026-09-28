#pragma once

#include "thread.hpp"

#include <stdint.h>

namespace scheduler {

/** Internal non-returning handoff used by the thread bootstrap. */
[[noreturn]] void thread_exit(kernel_thread::Thread* thread, int64_t status);

/** Internal timer-IRQ exit hook called after irq0 saves its registers. */
extern "C" void scheduler_interrupt_exit();

} // namespace scheduler
