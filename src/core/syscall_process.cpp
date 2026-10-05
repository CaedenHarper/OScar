#include "interrupts.hpp"
#include "loader.hpp"
#include "process.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "syscall_internal.hpp"
#include "syscalls.hpp"
#include "thread.hpp"
#include "user_memory.hpp"

#include <stdint.h>

#ifdef OSCAR_TEST_SUITE
#include "io.hpp"
#endif

namespace syscall_detail {

int64_t get_pid() {
    auto* thread = scheduler::current();
    auto* owner = kernel_thread::owner_process(thread);
    return owner == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(process::id(owner));
}

int64_t get_id() {
    auto* thread = scheduler::current();
    return thread == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(kernel_thread::id(thread));
}

int64_t get_process_info(const syscalls::Frame* frame) {
    if(frame->rsi == 0 || !user_memory::validate(frame->rsi, sizeof(UserProcessInfo), true)) {
        return kErrorInvalidArgument;
    }

    process::Info info = {};
    if(!process::info(frame->rdi, &info)) {
        return kErrorNotFound;
    }
    UserProcessInfo user_info = {
        .id = info.id,
        .state = static_cast<uint32_t>(info.state),
        .thread_count = info.thread_count,
        .user_page_count = info.user_page_count,
        .image_path = {},
    };
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for(uint32_t index = 0; index <= process::kMaximumImagePathLength; ++index) {
        user_info.image_path[index] = info.image_path[index];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return user_memory::copy_to_user(frame->rsi, &user_info, sizeof(user_info)) ? 0 : kErrorInvalidArgument;
}

int64_t kill_process(const syscalls::Frame* frame) {
    auto* thread = scheduler::current();
    auto* owner = kernel_thread::owner_process(thread);
    if(owner == nullptr || frame->rdi == 0) {
        return kErrorInvalidArgument;
    }
    auto* target = process::find(frame->rdi);
    if(target == nullptr) {
        return kErrorNotFound;
    }
    if(target == owner) {
        scheduler::thread_exit(thread, kKillExitStatus);
    }
    // The scheduler removes ready and wait-queue threads before detaching their address
    // space. This ordering prevents a killed process from being selected after CR3
    // teardown has begun.
    return scheduler::terminate_process(target, kKillExitStatus) ? 0 : kErrorIo;
}

#ifdef OSCAR_TEST_SUITE
[[noreturn]] void complete_test_suite() {
    // QEMU's isa-debug-exit device turns this privileged port write into a
    // clean emulator exit, so the test runner does not need to wait for the
    // kernel's idle loop after the user-space pass marker is printed.
    io::out8(kTestExitPort, kTestExitCode);
    for(;;) {
        asm volatile("pause");
    }
}
#endif

int64_t spawn(const syscalls::Frame* frame) {
    auto* parent = current_process();
    if(parent == nullptr) {
        return kErrorInvalidArgument;
    }

    char path[kMaximumPathLength + 1];
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!copy_process_path(frame, parent, path)) {
        return kErrorInvalidArgument;
    }

    char argument_storage[loader::kMaximumArgumentBytes];
    const char* arguments[loader::kMaximumArguments];
    uint32_t argument_count = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!copy_spawn_arguments(frame->rsi, &argument_storage[0], &arguments[0], &argument_count)) {
        return kErrorInvalidArgument;
    }

    process::Process* child = nullptr;
    kernel_thread::Thread* thread = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!loader::load_path(path, parent, arguments, argument_count, &child, &thread) || child == nullptr ||
       thread == nullptr) {
        return kErrorNotFound;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    const bool queued = scheduler::enqueue(thread);
    interrupts::restore(previous_state);
    if(!queued) {
        (void)kernel_thread::destroy(thread);
        (void)process::destroy(child);
        return kErrorIo;
    }
    return static_cast<int64_t>(process::id(child));
}

int64_t wait_pid(const syscalls::Frame* frame) {
    auto* parent = current_process();
    if(parent == nullptr || frame->rdi == 0 ||
       (frame->rsi != 0 && !user_memory::validate(frame->rsi, sizeof(int64_t), true))) {
        return kErrorInvalidArgument;
    }

    for(;;) {
        const interrupts::State previous_state = interrupts::save_and_disable();
        auto* child = process::find_child_locked(parent, frame->rdi);
        if(child == nullptr) {
            interrupts::restore(previous_state);
            return kErrorNotFound;
        }

        if(process::state(child) != process::State::Terminated) {
            // The child lookup and enqueue are one interrupt-disabled transaction, so
            // an exit cannot signal the parent between the check and the block.
            const bool blocked = scheduler::block_current(&parent->child_waiters);
            interrupts::restore(previous_state);
            if(!blocked) {
                return kErrorIo;
            }
            continue;
        }

        int64_t status = 0;
        if(frame->rsi != 0) {
            // The pointer was validated before entering the loop and interrupts are
            // disabled, so status delivery cannot be separated from child reaping.
            if(!user_memory::copy_to_user(frame->rsi, &child->exit_status, sizeof(status))) {
                interrupts::restore(previous_state);
                return kErrorInvalidArgument;
            }
        }
        if(!process::reap_child_locked(parent, child, &status)) {
            interrupts::restore(previous_state);
            return kErrorIo;
        }
        const auto child_id = process::id(child);
        interrupts::restore(previous_state);
        // The scheduler still owns the terminated thread record. It will destroy the
        // process and its address space after observing the parent detachment; freeing
        // the process here would leave that record with a dangling Process pointer.
        return static_cast<int64_t>(child_id);
    }
}

} // namespace syscall_detail
