#include "vfs.hpp"

#include "ext2.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             readability-magic-numbers, bugprone-easily-swappable-parameters)

namespace vfs {

namespace {

constexpr uint32_t kMaximumPathComponentLength = 255;
constexpr uint32_t kMaximumPathLength = 511;

ext2::FileSystem g_root_filesystem = {};
bool g_mounted = false;

Status translate_status(ext2::Status status) {
    switch(status) {
        case ext2::Status::Success:
            return Status::Success;
        case ext2::Status::NotFound:
            return Status::NotFound;
        case ext2::Status::Exists:
            return Status::Exists;
        case ext2::Status::NotDirectory:
            return Status::NotDirectory;
        case ext2::Status::IsDirectory:
            return Status::IsDirectory;
        case ext2::Status::NotEmpty:
            return Status::NotEmpty;
        case ext2::Status::NoSpace:
            return Status::NoSpace;
        case ext2::Status::IoError:
            return Status::IoError;
        case ext2::Status::Unsupported:
            return Status::Unsupported;
        case ext2::Status::InvalidArgument:
        case ext2::Status::NotMounted:
        case ext2::Status::Corrupt:
            return Status::IoError;
    }
    return Status::IoError;
}

bool split_parent(const char* path, char* parent, char* name) {
    if(path == nullptr || parent == nullptr || name == nullptr || *path != '/') {
        return false;
    }
    uint32_t length = 0;
    while(path[length] != '\0') {
        if(length == kMaximumPathLength) {
            return false;
        }
        ++length;
    }
    if(length < 2 || path[length - 1] == '/') {
        return false;
    }
    uint32_t slash = length;
    while(slash > 0 && path[slash - 1] != '/') {
        --slash;
    }
    const uint32_t name_length = length - slash;
    if(name_length == 0 || name_length > kMaximumPathComponentLength) {
        return false;
    }
    for(uint32_t index = 0; index < name_length; ++index) {
        name[index] = path[slash + index];
    }
    name[name_length] = '\0';
    if(slash == 1) {
        parent[0] = '/';
        parent[1] = '\0';
        return true;
    }
    const uint32_t parent_length = slash - 1;
    if(parent_length == 0 || parent_length > kMaximumPathLength) {
        return false;
    }
    for(uint32_t index = 0; index < parent_length; ++index) {
        parent[index] = path[index];
    }
    parent[parent_length] = '\0';
    return true;
}

Node node_from_inode(const ext2::Inode& inode) {
    return {
        inode.number,
        inode.size,
        inode.mode,
        inode.uid,
        inode.gid,
        inode.directory ? NodeType::Directory : NodeType::Regular
    };
}

bool has_access(const Node& node, Credentials credentials, uint8_t requested) {
    if(credentials.uid == 0) {
        return true;
    }
    const uint16_t permission_bits = credentials.uid == node.uid   ? static_cast<uint16_t>((node.mode >> 6U) & 7U)
                                     : credentials.gid == node.gid ? static_cast<uint16_t>((node.mode >> 3U) & 7U)
                                                                   : static_cast<uint16_t>(node.mode & 7U);
    return (permission_bits & requested) == requested;
}

Status lookup_child(const Node& directory, const char* name, Node* child) {
    ext2::Inode directory_inode = {};
    ext2::Inode child_inode = {};
    ext2::Status result =
        ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(directory.identifier), &directory_inode);
    if(result != ext2::Status::Success) {
        return translate_status(result);
    }
    result = ext2::lookup_child(&g_root_filesystem, &directory_inode, name, &child_inode);
    if(result == ext2::Status::Success) {
        *child = node_from_inode(child_inode);
    }
    return translate_status(result);
}

bool next_component(const char** path, char* component, uint32_t* length) {
    while(**path == '/') {
        ++*path;
    }
    if(**path == '\0') {
        return false;
    }
    uint32_t count = 0;
    while((*path)[count] != '\0' && (*path)[count] != '/') {
        if(count == kMaximumPathComponentLength) {
            component[count] = '\0';
            *length = count + 1;
            while((*path)[count] != '\0' && (*path)[count] != '/') {
                ++count;
            }
            *path += count;
            return true;
        }
        component[count] = (*path)[count];
        ++count;
    }
    component[count] = '\0';
    *length = count;
    *path += count;
    return true;
}

} // namespace

bool mount_root(block_device::Device* device) {
    if(g_mounted) {
        return true;
    }
    g_mounted = ext2::mount(device, &g_root_filesystem);
    return g_mounted;
}

bool is_mounted() {
    return g_mounted;
}

Status statfs(FileSystemStatus* status) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    if(status == nullptr) {
        return Status::InvalidArgument;
    }
    ext2::Statistics statistics = {};
    const Status result = translate_status(ext2::statistics(&g_root_filesystem, &statistics));
    if(result != Status::Success) {
        return result;
    }
    *status = {
        .block_size = statistics.block_size,
        .total_blocks = statistics.total_blocks,
        .free_blocks = statistics.free_blocks,
        .total_inodes = statistics.total_inodes,
        .free_inodes = statistics.free_inodes,
        .filesystem = {},
        .device = {},
        .mount_point = {},
    };
    constexpr char kFilesystem[] = "ext2";
    constexpr char kDevice[] = "virtio0";
    constexpr char kMountPoint[] = "/";
    for(uint32_t index = 0; index < sizeof(kFilesystem); ++index) {
        status->filesystem[index] = kFilesystem[index];
    }
    for(uint32_t index = 0; index < sizeof(kDevice); ++index) {
        status->device[index] = kDevice[index];
    }
    for(uint32_t index = 0; index < sizeof(kMountPoint); ++index) {
        status->mount_point[index] = kMountPoint[index];
    }
    return Status::Success;
}

Status resolve_as(const char* path, Node* node, Credentials credentials) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    if(path == nullptr || node == nullptr || *path == '\0') {
        return Status::InvalidArgument;
    }
    ext2::Inode root_inode = {};
    if(ext2::get_inode(&g_root_filesystem, 2, &root_inode) != ext2::Status::Success || !root_inode.directory) {
        return Status::IoError;
    }
    Node current = node_from_inode(root_inode);
    const char* remaining = path;
    char component[kMaximumPathComponentLength + 1];
    uint32_t component_length = 0;
    while(next_component(&remaining, component, &component_length)) {
        if(component_length == 1 && component[0] == '.') {
            continue;
        }
        if(component_length == 2 && component[0] == '.' && component[1] == '.') {
            return Status::Unsupported;
        }
        if(current.type != NodeType::Directory) {
            return Status::NotDirectory;
        }
        if(!has_access(current, credentials, kAccessExecute)) {
            return Status::PermissionDenied;
        }
        const Status result = lookup_child(current, component, &current);
        if(result != Status::Success) {
            return result;
        }
    }
    *node = current;
    return Status::Success;
}

Status resolve(const char* path, Node* node) {
    return resolve_as(path, node, {.uid = 0, .gid = 0});
}

Status read_directory_as(const char* path, uint32_t index, DirectoryEntry* entry, Credentials credentials) {
    if(!g_mounted || path == nullptr || entry == nullptr) {
        return g_mounted ? Status::InvalidArgument : Status::NotMounted;
    }
    Node directory = {};
    const Status resolve_status = resolve_as(path, &directory, credentials);
    if(resolve_status != Status::Success) {
        return resolve_status;
    }
    if(directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if(!has_access(directory, credentials, kAccessRead | kAccessExecute)) {
        return Status::PermissionDenied;
    }
    ext2::Inode directory_inode = {};
    if(ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(directory.identifier), &directory_inode) !=
       ext2::Status::Success) {
        return Status::IoError;
    }
    char name[256];
    ext2::Inode child = {};
    const ext2::Status read_status =
        ext2::read_directory(&g_root_filesystem, &directory_inode, index, &name[0], sizeof(name), &child);
    if(read_status != ext2::Status::Success) {
        return translate_status(read_status);
    }
    for(uint32_t character = 0; character < sizeof(name); ++character) {
        entry->name[character] = name[character];
        if(name[character] == '\0') {
            break;
        }
    }
    entry->node = node_from_inode(child);
    return Status::Success;
}

Status read_directory(const char* path, uint32_t index, DirectoryEntry* entry) {
    return read_directory_as(path, index, entry, {.uid = 0, .gid = 0});
}

Status open_as(const char* path, uint32_t flags, File* file, Credentials credentials) {
    if(file == nullptr || (flags & ~kOpenReadWrite) != 0 || (flags & kOpenReadWrite) == 0) {
        return Status::InvalidArgument;
    }
    Node node = {};
    const Status result = resolve_as(path, &node, credentials);
    if(result != Status::Success) {
        return result;
    }
    uint8_t requested = 0;
    if((flags & kOpenRead) != 0) {
        requested |= kAccessRead;
    }
    if((flags & kOpenWrite) != 0) {
        requested |= kAccessWrite;
    }
    if(!has_access(node, credentials, requested)) {
        return Status::PermissionDenied;
    }
    *file = {node, 0, flags, true};
    return Status::Success;
}

Status open(const char* path, uint32_t flags, File* file) {
    return open_as(path, flags, file, {.uid = 0, .gid = 0});
}

Status read(File* file, void* buffer, uint32_t length, uint32_t* bytes_read) {
    if(file == nullptr || !file->open || (file->flags & kOpenRead) == 0 || bytes_read == nullptr ||
       (length != 0 && buffer == nullptr)) {
        return Status::InvalidArgument;
    }
    if(file->node.type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    ext2::Inode inode = {};
    const ext2::Status inode_status =
        ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(file->node.identifier), &inode);
    if(inode_status != ext2::Status::Success) {
        return translate_status(inode_status);
    }
    const ext2::Status result = ext2::read_file(&g_root_filesystem, &inode, file->offset, length, buffer, bytes_read);
    if(result == ext2::Status::Success) {
        file->offset += *bytes_read;
    }
    return translate_status(result);
}

Status write(File* file, const void* buffer, uint32_t length, uint32_t* bytes_written) {
    if(file == nullptr || !file->open || (file->flags & kOpenWrite) == 0 || bytes_written == nullptr ||
       (length != 0 && buffer == nullptr)) {
        return Status::InvalidArgument;
    }
    if(file->node.type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    ext2::Inode inode = {};
    const ext2::Status inode_status =
        ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(file->node.identifier), &inode);
    if(inode_status != ext2::Status::Success) {
        return translate_status(inode_status);
    }
    const ext2::Status result =
        ext2::write_file(&g_root_filesystem, &inode, file->offset, length, buffer, bytes_written);
    if(result == ext2::Status::Success) {
        file->offset += *bytes_written;
        file->node.size = file->offset > file->node.size ? file->offset : file->node.size;
    }
    return translate_status(result);
}

Status create_as(const char* path, Node* node, Credentials credentials) {
    if(!g_mounted || node == nullptr) {
        return g_mounted ? Status::InvalidArgument : Status::NotMounted;
    }
    char parent_path[kMaximumPathLength + 1];
    char name[kMaximumPathComponentLength + 1];
    if(!split_parent(path, parent_path, name)) {
        return Status::InvalidArgument;
    }
    Node parent_node = {};
    const Status parent_status = resolve_as(parent_path, &parent_node, credentials);
    if(parent_status != Status::Success) {
        return parent_status;
    }
    if(parent_node.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if(!has_access(parent_node, credentials, kAccessWrite | kAccessExecute)) {
        return Status::PermissionDenied;
    }
    ext2::Inode parent = {};
    if(ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(parent_node.identifier), &parent) !=
       ext2::Status::Success) {
        return Status::IoError;
    }
    ext2::Inode created = {};
    const Status result = translate_status(ext2::create(&g_root_filesystem, &parent, name, false, &created));
    if(result == Status::Success) {
        *node = node_from_inode(created);
    }
    return result;
}

Status create(const char* path, Node* node) {
    return create_as(path, node, {.uid = 0, .gid = 0});
}

Status mkdir_as(const char* path, Credentials credentials) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    char parent_path[kMaximumPathLength + 1];
    char name[kMaximumPathComponentLength + 1];
    if(!split_parent(path, parent_path, name)) {
        return Status::InvalidArgument;
    }
    Node parent_node = {};
    const Status parent_status = resolve_as(parent_path, &parent_node, credentials);
    if(parent_status != Status::Success) {
        return parent_status;
    }
    if(parent_node.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if(!has_access(parent_node, credentials, kAccessWrite | kAccessExecute)) {
        return Status::PermissionDenied;
    }
    ext2::Inode parent = {};
    if(ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(parent_node.identifier), &parent) !=
       ext2::Status::Success) {
        return Status::IoError;
    }
    return translate_status(ext2::create(&g_root_filesystem, &parent, name, true, nullptr));
}

Status mkdir(const char* path) {
    return mkdir_as(path, {.uid = 0, .gid = 0});
}

Status unlink_as(const char* path, Credentials credentials) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    char parent_path[kMaximumPathLength + 1];
    char name[kMaximumPathComponentLength + 1];
    if(!split_parent(path, parent_path, name)) {
        return Status::InvalidArgument;
    }
    Node parent_node = {};
    const Status parent_status = resolve_as(parent_path, &parent_node, credentials);
    if(parent_status != Status::Success) {
        return parent_status;
    }
    if(parent_node.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if(!has_access(parent_node, credentials, kAccessWrite | kAccessExecute)) {
        return Status::PermissionDenied;
    }
    ext2::Inode parent = {};
    if(ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(parent_node.identifier), &parent) !=
       ext2::Status::Success) {
        return Status::IoError;
    }
    return translate_status(ext2::unlink(&g_root_filesystem, &parent, name));
}

Status unlink(const char* path) {
    return unlink_as(path, {.uid = 0, .gid = 0});
}

Status rmdir_as(const char* path, Credentials credentials) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    char parent_path[kMaximumPathLength + 1];
    char name[kMaximumPathComponentLength + 1];
    if(!split_parent(path, parent_path, name)) {
        return Status::InvalidArgument;
    }
    Node parent_node = {};
    const Status parent_status = resolve_as(parent_path, &parent_node, credentials);
    if(parent_status != Status::Success) {
        return parent_status;
    }
    if(parent_node.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if(!has_access(parent_node, credentials, kAccessWrite | kAccessExecute)) {
        return Status::PermissionDenied;
    }
    ext2::Inode parent = {};
    if(ext2::get_inode(&g_root_filesystem, static_cast<uint32_t>(parent_node.identifier), &parent) !=
       ext2::Status::Success) {
        return Status::IoError;
    }
    return translate_status(ext2::remove_directory(&g_root_filesystem, &parent, name));
}

Status rmdir(const char* path) {
    return rmdir_as(path, {.uid = 0, .gid = 0});
}

Status seek(File* file, uint64_t offset) {
    if(file == nullptr || !file->open) {
        return Status::InvalidArgument;
    }
    file->offset = offset;
    return Status::Success;
}

Status close(File* file) {
    if(file == nullptr || !file->open) {
        return Status::InvalidArgument;
    }
    file->open = false;
    return Status::Success;
}

} // namespace vfs

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-magic-numbers, bugprone-easily-swappable-parameters)
