#pragma once

#include "block_device.hpp"

#include <stdint.h>

namespace ext2 {

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    NotMounted,
    NotFound,
    NotDirectory,
    Corrupt,
    IoError,
    Unsupported,
};

struct Inode {
    uint32_t number;
    uint16_t mode;
    uint64_t size;
    bool directory;
};

struct FileSystem {
    block_device::Device* device;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t inode_table_blocks;
    bool mounted;
};

/**
 * Mount a read-only ext2 filesystem from the complete block device.
 * The block-device protocol and physical-memory allocator must be initialized first.
 * The filesystem does not take ownership of device and must remain mounted while
 * the device is used. Returns false when the superblock or metadata is unsupported.
 */
bool mount(block_device::Device* device, FileSystem* file_system);

/**
 * Look up an absolute or root-relative path in a mounted filesystem.
 * Components are separated by '/', repeated separators are accepted, and '.' is
 * supported; '..' is intentionally unsupported until directory-parent semantics exist.
 * Returns NotFound for a missing component and NotDirectory when traversal reaches a file.
 */
Status lookup(const FileSystem* file_system, const char* path, Inode* inode);

} // namespace ext2
