#include "descriptor_table.hpp"
#include "process.hpp"
#include "process_internal.hpp"
#include "syscall_internal.hpp"
#include "syscalls.hpp"
#include "user_memory.hpp"
#include "vfs.hpp"

#include <stdint.h>

namespace syscall_detail {

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
    const int32_t descriptor = owner == nullptr ? -1 : descriptor::allocate_file(&owner->descriptors, &file);
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

} // namespace syscall_detail
