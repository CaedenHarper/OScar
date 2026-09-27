#include "vfs.hpp"

#include "ext2.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             readability-magic-numbers, bugprone-easily-swappable-parameters)

namespace vfs {

namespace {

constexpr uint32_t kMaximumPathComponentLength = 255;

ext2::FileSystem g_root_filesystem = {};
bool g_mounted = false;

Status translate_status(ext2::Status status) {
    switch(status) {
        case ext2::Status::Success:
            return Status::Success;
        case ext2::Status::NotFound:
            return Status::NotFound;
        case ext2::Status::NotDirectory:
            return Status::NotDirectory;
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

Node node_from_inode(const ext2::Inode& inode) {
    return {inode.number, inode.size, inode.directory ? NodeType::Directory : NodeType::Regular};
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

Status resolve(const char* path, Node* node) {
    if(!g_mounted) {
        return Status::NotMounted;
    }
    if(path == nullptr || node == nullptr || *path == '\0') {
        return Status::InvalidArgument;
    }
    Node current = {2, 0, NodeType::Directory};
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
        const Status result = lookup_child(current, component, &current);
        if(result != Status::Success) {
            return result;
        }
    }
    *node = current;
    return Status::Success;
}

Status open(const char* path, uint32_t flags, File* file) {
    if(file == nullptr || (flags & ~kOpenRead) != 0 || (flags & kOpenRead) == 0) {
        return Status::InvalidArgument;
    }
    Node node = {};
    const Status result = resolve(path, &node);
    if(result != Status::Success) {
        return result;
    }
    *file = {node, 0, flags, true};
    return Status::Success;
}

Status read(File* file, void* buffer, uint32_t length, uint32_t* bytes_read) {
    if(file == nullptr || !file->open || bytes_read == nullptr || (length != 0 && buffer == nullptr)) {
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
