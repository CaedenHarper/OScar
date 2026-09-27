#include "syscalls.hpp"

#include "process.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "serial.hpp"
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

int64_t write(const syscalls::Frame* frame) {
    if(frame->rsi > kMaximumWriteLength) {
        return kErrorInvalidArgument;
    }

    constexpr uint64_t kBufferSize = 128;
    char buffer[kBufferSize];
    uint64_t copied = 0;
    while(copied < frame->rsi) {
        const uint64_t chunk = frame->rsi - copied > kBufferSize ? kBufferSize : frame->rsi - copied;
        if(!user_memory::copy_from_user(static_cast<void*>(buffer), frame->rdi + copied, chunk)) {
            return kErrorInvalidArgument;
        }
        for(uint64_t index = 0; index < chunk; ++index) {
            serial::putc(buffer[index]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
        copied += chunk;
    }
    return static_cast<int64_t>(copied);
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

int64_t read(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || process::file_descriptor(owner, frame->rdi) == nullptr) {
        return kErrorBadDescriptor;
    }
    if(frame->rdx > kMaximumReadLength || (frame->rdx != 0 && !user_memory::validate(frame->rsi, frame->rdx, true))) {
        return kErrorInvalidArgument;
    }
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
            scheduler::thread_exit(scheduler::current());
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
        default:
            frame->rax = static_cast<uint64_t>(kErrorUnknownCall);
            return;
    }
}

} // namespace syscalls
