#pragma once

#include "thread.hpp"

namespace scheduler {

/** Internal non-returning handoff used by the thread bootstrap. */
[[noreturn]] void thread_exit(kernel_thread::Thread* thread);

/** Internal timer-IRQ exit hook called after irq0 saves its registers. */
extern "C" void scheduler_interrupt_exit();

} // namespace scheduler
