#pragma once

#include "thread.hpp"

namespace scheduler {

/** Internal non-returning handoff used by the thread bootstrap. */
[[noreturn]] void thread_exit(kernel_thread::Thread* thread);

} // namespace scheduler
