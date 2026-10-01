#include "syscalls.hpp"

#include "interrupts.hpp"
#ifdef OSCAR_TEST_SUITE
#include "io.hpp"
#endif
#include "loader.hpp"
#include "network.hpp"
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
constexpr uint64_t kChdir = 16;
constexpr uint64_t kGetcwd = 17;
constexpr uint64_t kStat = 18;
constexpr uint64_t kReaddir = 19;
constexpr uint64_t kGetProcessInfo = 20;
constexpr uint64_t kKill = 21;
constexpr uint64_t kDup = 23;
constexpr uint64_t kDup2 = 24;
constexpr uint64_t kPipe = 25;
constexpr uint64_t kStatfs = 26;
constexpr uint64_t kPing = 27;
#ifdef OSCAR_TEST_SUITE
constexpr uint64_t kTestComplete = 22;
constexpr uint16_t kTestExitPort = 0xf4;
constexpr uint8_t kTestExitCode = 0x10;
#endif
constexpr uint64_t kMaximumWriteLength = 4096;
constexpr uint64_t kMaximumReadLength = 4096;
constexpr uint64_t kMaximumPathLength = 511;
constexpr uint64_t kMaximumNameLength = 255;
constexpr uint64_t kMaximumPathComponents = 256;
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
constexpr int64_t kErrorBufferTooSmall = -11;
constexpr int64_t kErrorPermissionDenied = -12;
constexpr int64_t kErrorNetworkUnavailable = -13;
constexpr int64_t kErrorNetworkTimeout = -14;
constexpr int64_t kErrorAddressUnreachable = -15;
constexpr int64_t kKillExitStatus = 137;

struct UserStat {
    uint64_t size;
    uint32_t type;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t reserved;
};

struct UserFileSystemStatus {
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    char filesystem[vfs::kFilesystemNameCapacity];
    char device[vfs::kDeviceNameCapacity];
    char mount_point[vfs::kMountPointCapacity];
};

struct UserDirectoryEntry {
    uint64_t identifier;
    uint64_t size;
    uint32_t type;
    uint32_t name_length;
    char name[kMaximumNameLength + 1];
};

struct UserProcessInfo {
    uint64_t id;
    uint32_t state;
    uint32_t thread_count;
    uint64_t user_page_count;
    char image_path[process::kMaximumImagePathLength + 1];
};

process::Process* current_process() {
    auto* thread = scheduler::current();
    return kernel_thread::owner_process(thread);
}

int64_t write_pipe(const syscalls::Frame* frame, process::Process* owner);

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

int64_t get_pid() {
    auto* thread = scheduler::current();
    auto* owner = kernel_thread::owner_process(thread);
    return owner == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(process::id(owner));
}

int64_t get_id() {
    auto* thread = scheduler::current();
    return thread == nullptr ? kErrorInvalidArgument : static_cast<int64_t>(kernel_thread::id(thread));
}

int64_t ping(const syscalls::Frame* frame) {
    if(frame->rdi == 0 || !user_memory::validate(frame->rdi, 4, false)) {
        return kErrorInvalidArgument;
    }
    uint8_t bytes[4];
    if(!user_memory::copy_from_user(bytes, frame->rdi, sizeof(bytes))) {
        return kErrorInvalidArgument;
    }
    const arp::Ipv4Address destination = {{bytes[0], bytes[1], bytes[2], bytes[3]}};
    const network::PingStatus result = network::ping(destination, frame->rsi, static_cast<uint16_t>(get_pid()));
    switch(result) {
        case network::PingStatus::Success:
            return 0;
        case network::PingStatus::NotInitialized:
            return kErrorNetworkUnavailable;
        case network::PingStatus::InvalidArgument:
            return kErrorInvalidArgument;
        case network::PingStatus::AddressUnreachable:
            return kErrorAddressUnreachable;
        case network::PingStatus::Timeout:
            return kErrorNetworkTimeout;
        case network::PingStatus::IoError:
            return kErrorIo;
    }
    return kErrorIo;
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

int64_t open(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr) {
        return kErrorInvalidArgument;
    }
    char path[kMaximumPathLength + 1];
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    if(!copy_process_path(frame, owner, path)) {
        return kErrorInvalidArgument;
    }
    vfs::File file = {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const auto credentials = process::credentials(owner);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const vfs::Status result =
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
        vfs::open_as(path, static_cast<uint32_t>(frame->rsi), &file, {.uid = credentials.uid, .gid = credentials.gid});
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
    if(owner == nullptr || !copy_process_path(frame, owner, &path[0])) {
        return kErrorInvalidArgument;
    }
    vfs::Node node = {};
    const auto credentials = process::credentials(owner);
    return translate_vfs_status(vfs::create_as(&path[0], &node, {.uid = credentials.uid, .gid = credentials.gid}));
}

using PathOperation = vfs::Status (*)(const char* path, vfs::Credentials credentials);

int64_t path_operation(const syscalls::Frame* frame, PathOperation operation) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || operation == nullptr || !copy_process_path(frame, owner, &path[0])) {
        return kErrorInvalidArgument;
    }
    const auto credentials = process::credentials(owner);
    return translate_vfs_status(operation(&path[0], {.uid = credentials.uid, .gid = credentials.gid}));
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-type-member-init)
int64_t change_directory(const syscalls::Frame* frame) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || !copy_process_path(frame, owner, &path[0])) {
        return kErrorInvalidArgument;
    }
    vfs::Node node = {};
    const auto credentials = process::credentials(owner);
    const vfs::Status result = vfs::resolve_as(&path[0], &node, {.uid = credentials.uid, .gid = credentials.gid});
    if(result != vfs::Status::Success) {
        return translate_vfs_status(result);
    }
    if(node.type != vfs::NodeType::Directory) {
        return kErrorIsDirectory;
    }
    if(credentials.uid != 0 && (credentials.uid == node.uid   ? (node.mode & 0100U) == 0
                                : credentials.gid == node.gid ? (node.mode & 0010U) == 0
                                                              : (node.mode & 0001U) == 0)) {
        return kErrorInvalidArgument;
    }
    for(uint32_t index = 0; path[index] != '\0'; ++index) {
        owner->working_directory[index] = path[index];
    }
    uint32_t length = 0;
    while(path[length] != '\0') {
        ++length;
    }
    owner->working_directory[length] = '\0';
    return 0;
}

int64_t get_working_directory(const syscalls::Frame* frame) {
    auto* owner = current_process();
    if(owner == nullptr || frame->rdi == 0 || frame->rsi == 0) {
        return kErrorInvalidArgument;
    }
    uint32_t length = 0;
    while(owner->working_directory[length] != '\0') {
        ++length;
    }
    if(frame->rsi <= length || !user_memory::validate(frame->rdi, length + 1, true) ||
       !user_memory::copy_to_user(frame->rdi, owner->working_directory, length + 1)) {
        return kErrorBufferTooSmall;
    }
    return length;
}

int64_t stat_path(const syscalls::Frame* frame) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || frame->rsi == 0 || !copy_process_path(frame, owner, &path[0]) ||
       !user_memory::validate(frame->rsi, sizeof(UserStat), true)) {
        return kErrorInvalidArgument;
    }
    vfs::Node node = {};
    const auto credentials = process::credentials(owner);
    const vfs::Status result = vfs::resolve_as(&path[0], &node, {.uid = credentials.uid, .gid = credentials.gid});
    if(result != vfs::Status::Success) {
        return translate_vfs_status(result);
    }
    UserStat stat = {
        .size = node.size,
        .type = node.type == vfs::NodeType::Directory ? 1U : 0U,
        .mode = node.mode,
        .uid = node.uid,
        .gid = node.gid,
        .reserved = 0,
    };
    return user_memory::copy_to_user(frame->rsi, &stat, sizeof(stat)) ? 0 : kErrorInvalidArgument;
}

int64_t stat_filesystem(const syscalls::Frame* frame) {
    if(frame->rdi == 0 || !user_memory::validate(frame->rdi, sizeof(UserFileSystemStatus), true)) {
        return kErrorInvalidArgument;
    }
    vfs::FileSystemStatus status = {};
    if(vfs::statfs(&status) != vfs::Status::Success) {
        return kErrorIo;
    }
    UserFileSystemStatus user_status = {
        .block_size = status.block_size,
        .total_blocks = status.total_blocks,
        .free_blocks = status.free_blocks,
        .total_inodes = status.total_inodes,
        .free_inodes = status.free_inodes,
        .filesystem = {},
        .device = {},
        .mount_point = {},
    };
    for(uint32_t index = 0; index < sizeof(user_status.filesystem); ++index) {
        user_status.filesystem[index] = status.filesystem[index];
    }
    for(uint32_t index = 0; index < sizeof(user_status.device); ++index) {
        user_status.device[index] = status.device[index];
    }
    for(uint32_t index = 0; index < sizeof(user_status.mount_point); ++index) {
        user_status.mount_point[index] = status.mount_point[index];
    }
    return user_memory::copy_to_user(frame->rdi, &user_status, sizeof(user_status)) ? 0 : kErrorInvalidArgument;
}

int64_t read_directory(const syscalls::Frame* frame) {
    auto* owner = current_process();
    char path[kMaximumPathLength + 1];
    if(owner == nullptr || frame->rdx == 0 || !copy_process_path(frame, owner, &path[0]) ||
       !user_memory::validate(frame->rdx, sizeof(UserDirectoryEntry), true)) {
        return kErrorInvalidArgument;
    }
    vfs::DirectoryEntry entry;
    const auto credentials = process::credentials(owner);
    const vfs::Status result = vfs::read_directory_as(
        &path[0], static_cast<uint32_t>(frame->rsi), &entry, {.uid = credentials.uid, .gid = credentials.gid}
    );
    if(result != vfs::Status::Success) {
        return translate_vfs_status(result);
    }
    UserDirectoryEntry user_entry;
    user_entry.identifier = entry.node.identifier;
    user_entry.size = entry.node.size;
    user_entry.type = entry.node.type == vfs::NodeType::Directory ? 1U : 0U;
    user_entry.name_length = 0;
    for(char& character : user_entry.name) {
        character = '\0';
    }
    while(user_entry.name_length < sizeof(entry.name) && entry.name[user_entry.name_length] != '\0') {
        user_entry.name[user_entry.name_length] = entry.name[user_entry.name_length];
        ++user_entry.name_length;
    }
    user_entry.name[user_entry.name_length] = '\0';
    return user_memory::copy_to_user(frame->rdx, &user_entry, sizeof(user_entry)) ? 0 : kErrorInvalidArgument;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-type-member-init)

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
        case kPing:
            frame->rax = static_cast<uint64_t>(ping(frame));
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
