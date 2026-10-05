#include "interrupts.hpp"
#include "network.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "syscall_internal.hpp"
#include "syscalls.hpp"
#include "terminal.hpp"
#include "user_memory.hpp"
#include "vfs.hpp"

#include <stdint.h>

namespace syscall_detail {

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
        case process::DescriptorKind::PipeRead:
            return read_pipe(frame, owner);
        case process::DescriptorKind::Invalid:
        case process::DescriptorKind::StandardOutput:
        case process::DescriptorKind::StandardError:
        case process::DescriptorKind::PipeWrite:
        case process::DescriptorKind::Socket:
            return kErrorBadDescriptor;
    }
    return kErrorBadDescriptor;
}

int64_t write_pipe(const syscalls::Frame* frame, process::Process* owner) {
    auto* pipe = process::pipe_descriptor(owner, frame->rdi, nullptr);
    if(pipe == nullptr) {
        return kErrorBadDescriptor;
    }
    if(frame->rdx == 0) {
        return 0;
    }
    uint64_t total = 0;
    while(total < frame->rdx) {
        uint8_t byte = 0;
        if(!user_memory::copy_from_user(&byte, frame->rsi + total, sizeof(byte))) {
            return total == 0 ? kErrorInvalidArgument : static_cast<int64_t>(total);
        }
        const interrupts::State previous_state = interrupts::save_and_disable();
        if(pipe->readers == 0) {
            interrupts::restore(previous_state);
            return total == 0 ? kErrorIo : static_cast<int64_t>(total);
        }
        if(pipe->bytes == process::kPipeCapacity) {
            const bool blocked = scheduler::block_current(&pipe->write_waiters);
            interrupts::restore(previous_state);
            if(!blocked) {
                return total == 0 ? kErrorIo : static_cast<int64_t>(total);
            }
            continue;
        }
        pipe->buffer[pipe->write_position] = byte; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        pipe->write_position = (pipe->write_position + 1) % process::kPipeCapacity;
        ++pipe->bytes;
        (void)scheduler::wake_all(&pipe->read_waiters);
        interrupts::restore(previous_state);
        ++total;
    }
    return static_cast<int64_t>(total);
}

int64_t duplicate(const syscalls::Frame* frame, bool explicit_target) {
    auto* owner = current_process();
    if(owner == nullptr || frame->rdi >= process::kMaximumFileDescriptors) {
        return kErrorBadDescriptor;
    }
    uint64_t target = frame->rsi;
    if(!explicit_target) {
        target = process::kMaximumFileDescriptors;
        for(uint64_t descriptor = process::kFirstFileDescriptor; descriptor < process::kMaximumFileDescriptors;
            ++descriptor) {
            if(process::descriptor_kind(owner, descriptor) == process::DescriptorKind::Invalid) {
                target = descriptor;
                break;
            }
        }
    }
    if(target >= process::kMaximumFileDescriptors) {
        return kErrorIo;
    }
    const int32_t result = process::duplicate_descriptor(owner, frame->rdi, target);
    return result < 0 ? kErrorBadDescriptor : result;
}

int64_t create_pipe(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || !user_memory::validate(frame->rdi, sizeof(int64_t) * 2, true)) {
        return kErrorInvalidArgument;
    }
    int64_t descriptors[2] = {-1, -1};
    if(!process::create_pipe(owner, &descriptors[0]) ||
       !user_memory::copy_to_user(frame->rdi, &descriptors[0], sizeof(descriptors))) {
        if(descriptors[0] >= 0) {
            (void)process::close_file_descriptor(owner, static_cast<uint64_t>(descriptors[0]));
        }
        if(descriptors[1] >= 0) {
            (void)process::close_file_descriptor(owner, static_cast<uint64_t>(descriptors[1]));
        }
        return kErrorIo;
    }
    return 0;
}

int64_t close(const syscalls::Frame* frame) {
    auto* owner = current_process();
    auto* socket = owner == nullptr ? nullptr : process::socket_descriptor(owner, frame->rdi);
    if(socket != nullptr && socket->references == 1) {
        (void)network::tcp_close(&socket->connection, kSocketTimeoutTicks);
    }
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

} // namespace syscall_detail
