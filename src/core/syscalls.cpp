#include "syscalls.hpp"

#include "interrupts.hpp"
#include "loader.hpp"
#include "process.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "syscall_internal.hpp"
#include "terminal.hpp"
#include "thread.hpp"
#include "user_memory.hpp"
#include "vfs.hpp"

#include <stdint.h>

namespace syscall_detail {

process::Process* current_process() {
    auto* thread = scheduler::current();
    return kernel_thread::owner_process(thread);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int64_t read_pipe(const syscalls::Frame* frame, process::Process* owner) {
    auto* pipe = process::pipe_descriptor(owner, frame->rdi, nullptr);
    if(pipe == nullptr || frame->rdx == 0) {
        return pipe == nullptr ? kErrorBadDescriptor : 0;
    }
    uint64_t total = 0;
    while(total < frame->rdx) {
        uint8_t byte = 0;
        const interrupts::State previous_state = interrupts::save_and_disable();
        if(pipe->bytes != 0) {
            // The ring index is bounded by kPipeCapacity while interrupts are disabled.
            byte = pipe->buffer[pipe->read_position]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            pipe->read_position = (pipe->read_position + 1) % process::kPipeCapacity;
            --pipe->bytes;
            (void)scheduler::wake_all(&pipe->write_waiters);
            interrupts::restore(previous_state);
            if(!user_memory::copy_to_user(frame->rsi + total, &byte, sizeof(byte))) {
                return total == 0 ? kErrorInvalidArgument : static_cast<int64_t>(total);
            }
            ++total;
            continue;
        }
        if(pipe->writers == 0) {
            interrupts::restore(previous_state);
            return static_cast<int64_t>(total);
        }
        const bool blocked = scheduler::block_current(&pipe->read_waiters);
        interrupts::restore(previous_state);
        if(!blocked) {
            return total == 0 ? kErrorIo : static_cast<int64_t>(total);
        }
    }
    return static_cast<int64_t>(total);
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
        case vfs::Status::PermissionDenied:
            return kErrorPermissionDenied;
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

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//             readability-math-missing-parentheses)
bool copy_spawn_arguments(uintptr_t user_arguments, char* storage, const char** arguments, uint32_t* argument_count) {
    if(storage == nullptr || arguments == nullptr || argument_count == nullptr) {
        return false;
    }
    *argument_count = 0;
    if(user_arguments == 0) {
        return true;
    }

    uint32_t storage_length = 0;
    for(uint32_t index = 0; index < loader::kMaximumArguments; ++index) {
        uintptr_t user_argument = 0;
        if(index > (UINTPTR_MAX - user_arguments) / sizeof(uintptr_t)) {
            return false;
        }
        const uintptr_t pointer_address = user_arguments + index * sizeof(uintptr_t);
        if(!user_memory::copy_from_user(&user_argument, pointer_address, sizeof(user_argument))) {
            return false;
        }
        if(user_argument == 0) {
            *argument_count = index;
            return true;
        }
        arguments[index] = storage + storage_length;
        for(;;) {
            if(storage_length >= loader::kMaximumArgumentBytes ||
               !user_memory::copy_from_user(storage + storage_length, user_argument, 1)) {
                return false;
            }
            if(storage[storage_length] == '\0') {
                ++storage_length;
                break;
            }
            ++storage_length;
            ++user_argument;
        }
    }
    return false;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay,
//           readability-math-missing-parentheses)

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             cppcoreguidelines-avoid-magic-numbers, readability-function-cognitive-complexity,
//             readability-math-missing-parentheses)
bool normalize_process_path(const process::Process* owner, const char* input, char* output) {
    if(owner == nullptr || input == nullptr || output == nullptr || *input == '\0') {
        return false;
    }
    char combined[(kMaximumPathLength * 2) + 2];
    uint32_t combined_length = 0;
    if(input[0] != '/') {
        while(owner->working_directory[combined_length] != '\0') {
            combined[combined_length] = owner->working_directory[combined_length];
            ++combined_length;
        }
        if(combined_length > 1) {
            combined[combined_length++] = '/';
        }
    }
    for(uint32_t index = 0; input[index] != '\0'; ++index) {
        if(combined_length >= sizeof(combined) - 1) {
            return false;
        }
        combined[combined_length++] = input[index];
    }
    combined[combined_length] = '\0';

    uint16_t component_starts[kMaximumPathComponents];
    uint32_t component_count = 0;
    uint32_t output_length = 1;
    output[0] = '/';
    for(uint32_t index = 0; index < combined_length;) {
        while(index < combined_length && combined[index] == '/') {
            ++index;
        }
        if(index == combined_length) {
            break;
        }
        const uint32_t component_start = index;
        while(index < combined_length && combined[index] != '/') {
            ++index;
        }
        const uint32_t component_length = index - component_start;
        if(component_length == 1 && combined[component_start] == '.') {
            continue;
        }
        if(component_length == 2 && combined[component_start] == '.' && combined[component_start + 1] == '.') {
            if(component_count != 0) {
                output_length = component_starts[--component_count];
            }
            continue;
        }
        if(component_length > kMaximumNameLength || component_count >= kMaximumPathComponents ||
           output_length + component_length + (output_length > 1 ? 1U : 0U) > kMaximumPathLength) {
            return false;
        }
        component_starts[component_count++] = static_cast<uint16_t>(output_length);
        if(output_length > 1) {
            output[output_length++] = '/';
        }
        for(uint32_t character = 0; character < component_length; ++character) {
            output[output_length++] = combined[component_start + character];
        }
    }
    output[output_length] = '\0';
    return true;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           cppcoreguidelines-avoid-magic-numbers, readability-function-cognitive-complexity,
//           readability-math-missing-parentheses)

bool copy_process_path(const syscalls::Frame* frame, const process::Process* owner, char* path) {
    char input[kMaximumPathLength + 1];
    return owner != nullptr && copy_path(frame->rdi, &input[0]) && normalize_process_path(owner, &input[0], path);
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
    if(kind == process::DescriptorKind::PipeWrite) {
        return write_pipe(frame, owner);
    }
    if(kind != process::DescriptorKind::StandardOutput && kind != process::DescriptorKind::StandardError) {
        return kErrorBadDescriptor;
    }
    return write_terminal(frame);
}

} // namespace syscall_detail

namespace syscalls {

using syscall_detail::change_directory;
using syscall_detail::close;
using syscall_detail::create_file;
using syscall_detail::create_pipe;
using syscall_detail::duplicate;
using syscall_detail::get_id;
using syscall_detail::get_pid;
using syscall_detail::get_process_info;
using syscall_detail::get_working_directory;
using syscall_detail::kChdir;
using syscall_detail::kClose;
using syscall_detail::kConnect;
using syscall_detail::kCreate;
using syscall_detail::kDup;
using syscall_detail::kDup2;
using syscall_detail::kErrorUnknownCall;
using syscall_detail::kExit;
using syscall_detail::kGetcwd;
using syscall_detail::kGetId;
using syscall_detail::kGetPid;
using syscall_detail::kGetProcessInfo;
using syscall_detail::kill_process;
using syscall_detail::kKill;
using syscall_detail::kMkdir;
using syscall_detail::kOpen;
using syscall_detail::kPing;
using syscall_detail::kPipe;
using syscall_detail::kRead;
using syscall_detail::kReaddir;
using syscall_detail::kRecv;
using syscall_detail::kResolve;
using syscall_detail::kRmdir;
using syscall_detail::kSeek;
using syscall_detail::kSend;
using syscall_detail::kSleep;
using syscall_detail::kSocket;
using syscall_detail::kSpawn;
using syscall_detail::kStat;
using syscall_detail::kStatfs;
using syscall_detail::kUnlink;
using syscall_detail::kWaitPid;
using syscall_detail::kWrite;
using syscall_detail::kYield;
using syscall_detail::open;
using syscall_detail::path_operation;
using syscall_detail::ping;
using syscall_detail::read;
using syscall_detail::read_directory;
using syscall_detail::resolve_hostname;
using syscall_detail::seek;
using syscall_detail::socket_connect;
using syscall_detail::socket_create;
using syscall_detail::socket_receive;
using syscall_detail::socket_send;
using syscall_detail::spawn;
using syscall_detail::stat_filesystem;
using syscall_detail::stat_path;
using syscall_detail::wait_pid;
using syscall_detail::write;
#ifdef OSCAR_TEST_SUITE
using syscall_detail::complete_test_suite;
using syscall_detail::kTestComplete;
#endif

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
        case kPing:
            frame->rax = static_cast<uint64_t>(ping(frame));
            return;
        case kResolve:
            frame->rax = static_cast<uint64_t>(resolve_hostname(frame));
            return;
        case kSocket:
            frame->rax = static_cast<uint64_t>(socket_create(frame));
            return;
        case kConnect:
            frame->rax = static_cast<uint64_t>(socket_connect(frame));
            return;
        case kSend:
            frame->rax = static_cast<uint64_t>(socket_send(frame));
            return;
        case kRecv:
            frame->rax = static_cast<uint64_t>(socket_receive(frame));
            return;
        case kGetProcessInfo:
            frame->rax = static_cast<uint64_t>(get_process_info(frame));
            return;
        case kKill:
            frame->rax = static_cast<uint64_t>(kill_process(frame));
            return;
        case kDup:
            frame->rax = static_cast<uint64_t>(duplicate(frame, false));
            return;
        case kDup2:
            frame->rax = static_cast<uint64_t>(duplicate(frame, true));
            return;
        case kPipe:
            frame->rax = static_cast<uint64_t>(create_pipe(frame));
            return;
#ifdef OSCAR_TEST_SUITE
        case kTestComplete:
            complete_test_suite();
#endif
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
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::mkdir_as));
            return;
        case kUnlink:
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::unlink_as));
            return;
        case kRmdir:
            frame->rax = static_cast<uint64_t>(path_operation(frame, vfs::rmdir_as));
            return;
        case kChdir:
            frame->rax = static_cast<uint64_t>(change_directory(frame));
            return;
        case kGetcwd:
            frame->rax = static_cast<uint64_t>(get_working_directory(frame));
            return;
        case kStat:
            frame->rax = static_cast<uint64_t>(stat_path(frame));
            return;
        case kReaddir:
            frame->rax = static_cast<uint64_t>(read_directory(frame));
            return;
        case kStatfs:
            frame->rax = static_cast<uint64_t>(stat_filesystem(frame));
            return;
        default:
            frame->rax = static_cast<uint64_t>(kErrorUnknownCall);
            return;
    }
}

} // namespace syscalls
