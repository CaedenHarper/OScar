#pragma once

#include "block_device.hpp"
#include "spinlock.hpp"

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
    mutable synchronization::Spinlock io_lock;
};

/**
 * Mount a read-only ext2 filesystem from the complete block device.
 * The block-device protocol and physical-memory allocator must be initialized first.
 * The filesystem does not take ownership of device and must remain mounted while
 * the device is used. Returns false when the superblock or metadata is unsupported.
 */
bool mount(block_device::Device* device, FileSystem* file_system);

/** Load one inode by number from a mounted filesystem. */
Status get_inode(const FileSystem* file_system, uint32_t inode_number, Inode* inode);

/** Look up one child name within a directory inode. */
Status lookup_child(const FileSystem* file_system, const Inode* directory, const char* name, Inode* inode);

/**
 * Look up an absolute or root-relative path in a mounted filesystem.
 * Components are separated by '/', repeated separators are accepted, and '.' is
 * supported; '..' is intentionally unsupported until directory-parent semantics exist.
 * Returns NotFound for a missing component and NotDirectory when traversal reaches a file.
 */
Status lookup(const FileSystem* file_system, const char* path, Inode* inode);

/**
 * Read bytes from a regular file inode at the supplied offset.
 * The destination is a kernel buffer owned by the caller. Reads stop at EOF,
 * return Success with zero bytes at EOF, and report Unsupported for files that
 * require indirect block traversal until that support is implemented.
 */
Status read_file(
    const FileSystem* file_system,
    const Inode* inode,
    uint64_t offset,
    uint32_t length,
    void* buffer,
    uint32_t* bytes_read
);

} // namespace ext2
