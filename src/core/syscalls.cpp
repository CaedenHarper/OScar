#include "syscalls.hpp"

#include "interrupts.hpp"
#include "loader.hpp"
#include "process.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "terminal.hpp"
#include "thread.hpp"
#include "user_memory.hpp"
#include "vfs.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t kWrite = 0;
constexpr uint64_t kExit = 1;
constexpr uint64_t kYield = 2;
constexpr uint64_t kSleep = 3;
constexpr uint64_t kGetPid = 4;
constexpr uint64_t kGetId = 5;
constexpr uint64_t kOpen = 6;
constexpr uint64_t kRead = 7;
constexpr uint64_t kClose = 8;
constexpr uint64_t kSeek = 9;
constexpr uint64_t kSpawn = 10;
constexpr uint64_t kWaitPid = 11;
constexpr uint64_t kCreate = 12;
constexpr uint64_t kMkdir = 13;
constexpr uint64_t kUnlink = 14;
constexpr uint64_t kRmdir = 15;
constexpr uint64_t kMaximumWriteLength = 4096;
constexpr uint64_t kMaximumReadLength = 4096;
constexpr uint64_t kMaximumPathLength = 255;
constexpr uint64_t kReadBufferSize = 128;
constexpr int64_t kErrorInvalidArgument = -1;
constexpr int64_t kErrorUnknownCall = -2;
constexpr int64_t kErrorNotFound = -3;
constexpr int64_t kErrorBadDescriptor = -4;
constexpr int64_t kErrorIsDirectory = -5;
constexpr int64_t kErrorIo = -6;
constexpr int64_t kErrorReadOnly = -7;
constexpr int64_t kErrorExists = -8;
constexpr int64_t kErrorNotEmpty = -9;
constexpr int64_t kErrorNoSpace = -10;

process::Process* current_process() {
    auto* thread = scheduler::current();
    return kernel_thread::owner_process(thread);
}

int64_t translate_vfs_status(vfs::Status status) {
    switch(status) {
        case vfs::Status::Success:
            return 0;
        case vfs::Status::NotFound:
            return kErrorNotFound;
        case vfs::Status::IsDirectory:
        case vfs::Status::NotDirectory:
            return kErrorIsDirectory;
        case vfs::Status::Exists:
            return kErrorExists;
        case vfs::Status::NotEmpty:
            return kErrorNotEmpty;
        case vfs::Status::NoSpace:
            return kErrorNoSpace;
        case vfs::Status::IoError:
            return kErrorIo;
        case vfs::Status::ReadOnly:
            return kErrorReadOnly;
        case vfs::Status::InvalidArgument:
        case vfs::Status::NotMounted:
        case vfs::Status::Unsupported:
            return kErrorInvalidArgument;
    }
    return kErrorIo;
}

bool copy_path(uintptr_t user_path, char* path) {
    if(user_path == 0 || path == nullptr) {
        return false;
    }
    for(uint64_t index = 0; index <= kMaximumPathLength; ++index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) validated byte-wise user path copy
        if(user_path > UINTPTR_MAX - index || !user_memory::copy_from_user(path + index, user_path + index, 1)) {
            return false;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) index is bounded by path capacity
        if(path[index] == '\0') {
            return true;
        }
    }
    return false;
}

int64_t write_file_descriptor(const syscalls::Frame* frame, process::Process* owner) {
    if(frame->rdx != 0 && !user_memory::validate(frame->rsi, frame->rdx, false)) {
        return kErrorInvalidArgument;
    }
    auto* file = process::file_descriptor(owner, frame->rdi);
    constexpr uint64_t kBufferSize = 128;
    char buffer[kBufferSize];
    uint64_t copied = 0;
    while(copied < frame->rdx) {
        const uint64_t chunk = frame->rdx - copied > sizeof(buffer) ? sizeof(buffer) : frame->rdx - copied;
        if(!user_memory::copy_from_user(static_cast<void*>(buffer), frame->rsi + copied, chunk)) {
            return copied == 0 ? kErrorInvalidArgument : static_cast<int64_t>(copied);
        }
        uint32_t written = 0;
        const vfs::Status result =
            vfs::write(file, static_cast<const void*>(buffer), static_cast<uint32_t>(chunk), &written);
        if(result != vfs::Status::Success) {
            return copied == 0 ? translate_vfs_status(result) : static_cast<int64_t>(copied);
        }
        copied += written;
        if(written < chunk) {
            break;
        }
    }
    return static_cast<int64_t>(copied);
}

int64_t write_terminal(const syscalls::Frame* frame) {
    constexpr uint64_t kBufferSize = 128;
    char buffer[kBufferSize];
    uint64_t copied = 0;
    while(copied < frame->rdx) {
        const uint64_t chunk = frame->rdx - copied > sizeof(buffer) ? sizeof(buffer) : frame->rdx - copied;
        if(!user_memory::copy_from_user(static_cast<void*>(buffer), frame->rsi + copied, chunk)) {
            return kErrorInvalidArgument;
        }
        if(terminal::write(&buffer[0], chunk) != static_cast<int64_t>(chunk)) {
            return copied == 0 ? kErrorIo : static_cast<int64_t>(copied);
        }
        copied += chunk;
    }
    return static_cast<int64_t>(copied);
}

int64_t write(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || frame->rdx > kMaximumWriteLength) {
        return kErrorInvalidArgument;
    }
    const process::DescriptorKind kind = process::descriptor_kind(owner, frame->rdi);
    if(kind == process::DescriptorKind::File) {
        return write_file_descriptor(frame, owner);
    }
    if(kind != process::DescriptorKind::StandardOutput && kind != process::DescriptorKind::StandardError) {
        return kErrorBadDescriptor;
    }
    return write_terminal(frame);
}

int64_t get_pid() {
    auto* thread = scheduler::current();
    auto* owner = kernel_thread::owner_process(thread);
    return owner == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(process::id(owner));
}

int64_t get_id() {
    auto* thread = scheduler::current();
    return thread == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(kernel_thread::id(thread));
}

int64_t open(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr) {
        return kErrorInvalidArgument;
    }
    char path[kMaximumPathLength + 1];
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!copy_path(frame->rdi, path)) {
        return kErrorInvalidArgument;
    }
    vfs::File file = {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const vfs::Status result = vfs::open(path, static_cast<uint32_t>(frame->rsi), &file);
    if(result != vfs::Status::Success) {
        return translate_vfs_status(result);
    }
    const int32_t descriptor = process::allocate_file_descriptor(owner, &file);
    if(descriptor < 0) {
        (void)vfs::close(&file);
        return kErrorIo;
    }
    return descriptor;
}

int64_t create_file(const syscalls::Frame* frame) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || !copy_path(frame->rdi, &path[0])) {
        return kErrorInvalidArgument;
    }
    vfs::Node node = {};
    return translate_vfs_status(vfs::create(&path[0], &node));
}

using PathOperation = vfs::Status (*)(const char* path);

int64_t path_operation(const syscalls::Frame* frame, PathOperation operation) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || operation == nullptr || !copy_path(frame->rdi, &path[0])) {
        return kErrorInvalidArgument;
    }
    return translate_vfs_status(operation(&path[0]));
}

int64_t read_terminal(const syscalls::Frame* frame) {
    constexpr uint64_t kTerminalBufferSize = 128;
    char buffer[kTerminalBufferSize];
    const uint64_t requested = frame->rdx < kTerminalBufferSize ? frame->rdx : kTerminalBufferSize;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const int64_t received = terminal::read(buffer, requested);
    if(received <= 0) {
        return received;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!user_memory::copy_to_user(frame->rsi, buffer, static_cast<uint64_t>(received))) {
        return kErrorInvalidArgument;
    }
    return received;
}

int64_t read_file(const syscalls::Frame* frame, process::Process* owner) {
    auto* file = process::file_descriptor(owner, frame->rdi);
    uint8_t buffer[kReadBufferSize];
    uint32_t total = 0;
    while(total < frame->rdx) {
        const auto remaining = static_cast<uint32_t>(frame->rdx - total);
        const uint32_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        uint32_t received = 0;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
        const vfs::Status result = vfs::read(file, buffer, chunk, &received);
        if(result != vfs::Status::Success) {
            return total == 0 ? translate_vfs_status(result) : total;
        }
        if(received == 0) {
            return total;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
        if(!user_memory::copy_to_user(frame->rsi + total, buffer, received)) {
            return total == 0 ? kErrorInvalidArgument : total;
        }
        total += received;
        if(received < chunk) {
            break;
        }
    }
    return total;
}

int64_t read(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr) {
        return kErrorBadDescriptor;
    }
    if(frame->rdx > kMaximumReadLength || (frame->rdx != 0 && !user_memory::validate(frame->rsi, frame->rdx, true))) {
        return kErrorInvalidArgument;
    }
    switch(process::descriptor_kind(owner, frame->rdi)) {
        case process::DescriptorKind::StandardInput:
            return read_terminal(frame);
        case process::DescriptorKind::File:
            return read_file(frame, owner);
        case process::DescriptorKind::Invalid:
        case process::DescriptorKind::StandardOutput:
        case process::DescriptorKind::StandardError:
            return kErrorBadDescriptor;
    }
    return kErrorBadDescriptor;
}

int64_t close(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || !process::close_file_descriptor(owner, frame->rdi)) {
        return kErrorBadDescriptor;
    }
    return 0;
}

int64_t seek(const syscalls::Frame* frame) {
    auto* owner = current_process();
    auto* file = owner == nullptr ? nullptr : process::file_descriptor(owner, frame->rdi);
    if(file == nullptr) {
        return kErrorBadDescriptor;
    }
    return translate_vfs_status(vfs::seek(file, frame->rsi));
}

int64_t spawn(const syscalls::Frame* frame) {
    auto* parent = current_process();
    if(parent == nullptr) {
        return kErrorInvalidArgument;
    }

    char path[kMaximumPathLength + 1];
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!copy_path(frame->rdi, path)) {
        return kErrorInvalidArgument;
    }

    process::Process* child = nullptr;
    kernel_thread::Thread* thread = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!loader::load_path(path, parent, &child, &thread) || child == nullptr || thread == nullptr) {
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
        if(!process::destroy(child)) {
            return kErrorIo;
        }
        return static_cast<int64_t>(child_id);
    }
}

} // namespace

namespace syscalls {

extern "C" void handle(Frame* frame) {
    if(frame == nullptr || (frame->cs & 3U) != 3U) {
        return;
    }

    switch(frame->rax) {
        case kWrite:
            frame->rax = static_cast<uint64_t>(write(frame));
            return;
        case kExit:
            scheduler::thread_exit(scheduler::current(), static_cast<int64_t>(frame->rdi));
        case kYield:
            scheduler::yield();
            frame->rax = 0;
            return;
        case kSleep:
            scheduler::sleep(frame->rdi);
            frame->rax = 0;
            return;
        case kGetPid:
            frame->rax = static_cast<uint64_t>(get_pid());
            return;
        case kGetId:
            frame->rax = static_cast<uint64_t>(get_id());
            return;
        case kOpen:
            frame->rax = static_cast<uint64_t>(open(frame));
            return;
        case kRead:
            frame->rax = static_cast<uint64_t>(read(frame));
            return;
        case kClose:
            frame->rax = static_cast<uint64_t>(close(frame));
            return;
        case kSeek:
            frame->rax = static_cast<uint64_t>(seek(frame));
            return;
        case kSpawn:
            frame->rax = static_cast<uint64_t>(spawn(frame));
            return;
        case kWaitPid:
            frame->rax = static_cast<uint64_t>(wait_pid(frame));
            return;
        case kCreate:
            frame->rax = static_cast<uint64_t>(create_file(frame));
            return;
        case kMkdir:
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::mkdir));
            return;
        case kUnlink:
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::unlink));
            return;
        case kRmdir:
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::rmdir));
            return;
        default:
            frame->rax = static_cast<uint64_t>(kErrorUnknownCall);
            return;
    }
}

} // namespace syscalls
