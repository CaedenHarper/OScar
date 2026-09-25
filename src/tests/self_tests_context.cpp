#include "context.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "thread.hpp"

#include <stdint.h>

namespace self_tests_detail {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//             clang-analyzer-core.FixedAddressDereference)
// Context-switch and kernel-thread lifecycle tests.

struct ContextTestState {
    context::CpuContext main_context;
    context::CpuContext thread_context;
    volatile uint64_t stage;
};

void thread_test_entry(void* argument);

void context_test_entry(void* argument) {
    auto* state = static_cast<ContextTestState*>(argument);
    state->stage = 1;
    context::switch_context(&state->thread_context, &state->main_context);
    state->stage = 2;
    context::switch_context(&state->thread_context, &state->main_context);
    __builtin_unreachable();
}
void test_context_switch() {
    static uint8_t thread_stack[kContextStackSize];
    ContextTestState state;
    state.stage = 0;
    if(!context::initialize(
           &state.thread_context,
           reinterpret_cast<uintptr_t>(thread_stack) + kContextStackSize,
           context_test_entry,
           &state
       )) {
        panic::halt("context smoke test could not initialize a thread context");
    }

    // Each switch resumes at the instruction after the previous switch call, proving that
    // the saved callee-saved registers and instruction pointer belong to the right context.
    context::switch_context(&state.main_context, &state.thread_context);
    if(state.stage != 1) {
        panic::halt("context smoke test did not enter the thread context");
    }
    context::switch_context(&state.main_context, &state.thread_context);
    if(state.stage != 2) {
        panic::halt("context smoke test did not resume the thread context");
    }
    serial::write("Kernel context switch smoke test passed.\n");
}

void thread_test_entry(void* argument) {
    auto* state = static_cast<ThreadTestState*>(argument);
    state->marker = 0x4f53636172544852ULL;
}

void test_kernel_thread() {
    ThreadTestState first_state = {};
    ThreadTestState second_state = {};
    auto* first = kernel_thread::create(thread_test_entry, &first_state, kThreadStackSize);
    auto* second = kernel_thread::create(thread_test_entry, &second_state, kThreadStackSize);
    // Compare bounds before running either thread: independent stacks are an ownership
    // guarantee, not merely an implementation detail of the allocator.
    const bool stacks_overlap = kernel_thread::stack_bottom(first) < kernel_thread::stack_top(second) &&
                                kernel_thread::stack_bottom(second) < kernel_thread::stack_top(first);
    if(first == nullptr || second == nullptr || kernel_thread::id(first) == 0 ||
       kernel_thread::id(first) == kernel_thread::id(second) ||
       kernel_thread::state(first) != kernel_thread::State::Ready ||
       kernel_thread::state(second) != kernel_thread::State::Ready ||
       kernel_thread::stack_top(first) - kernel_thread::stack_bottom(first) != kThreadStackSize ||
       kernel_thread::stack_top(second) - kernel_thread::stack_bottom(second) != kThreadStackSize || stacks_overlap) {
        panic::halt("kernel thread smoke test could not create independent threads");
    }

    context::CpuContext main_context = {};
    if(!kernel_thread::run(first, &main_context) || !kernel_thread::run(second, &main_context) ||
       first_state.marker != 0x4f53636172544852ULL || second_state.marker != 0x4f53636172544852ULL ||
       kernel_thread::state(first) != kernel_thread::State::Terminated ||
       kernel_thread::state(second) != kernel_thread::State::Terminated || !kernel_thread::destroy(first) ||
       !kernel_thread::destroy(second)) {
        panic::halt("kernel thread smoke test did not complete its bootstrap path");
    }
    serial::write("Kernel thread stack and lifecycle smoke test passed.\n");
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, performance-no-int-to-ptr,
//           clang-analyzer-core.FixedAddressDereference)

} // namespace self_tests_detail
