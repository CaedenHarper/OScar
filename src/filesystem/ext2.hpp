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
    Exists,
    NotDirectory,
    IsDirectory,
    NotEmpty,
    NoSpace,
    Corrupt,
    IoError,
    Unsupported,
};

constexpr uint32_t kInodeBlockPointerCount = 15;

struct Inode {
    uint32_t number;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint64_t size;
    uint32_t sectors;
    uint16_t links;
    uint32_t blocks[kInodeBlockPointerCount];
    bool directory;
};

struct FileSystem {
    block_device::Device* device;
    uint64_t block_count;
    uint32_t block_size;
    uint32_t first_data_block;
    uint32_t blocks_per_group;
    uint32_t inode_count;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    uint32_t inode_table_blocks;
    bool mounted;
    mutable synchronization::Spinlock io_lock;
};

/**
 * Mount an ext2 filesystem from the complete block device. Regular-file writes
 * are synchronous. Directory mutation is supported for regular files and
 * directories without hard links.
 * The block-device protocol and physical-memory allocator must be initialized first.
 * The filesystem does not take ownership of device and must remain mounted while
 * the device is used. Returns false when the superblock or metadata is unsupported.
 */
bool mount(block_device::Device* device, FileSystem* file_system);

/** Load one inode by number from a mounted filesystem. */
Status get_inode(const FileSystem* file_system, uint32_t inode_number, Inode* inode);

/** Look up one child name within a directory inode. */
Status lookup_child(const FileSystem* file_system, const Inode* directory, const char* name, Inode* inode);

/** Read the index-th non-dot entry from a directory inode, or NotFound at end. */
Status read_directory(
    const FileSystem* file_system,
    const Inode* directory,
    uint32_t index,
    char* name,
    uint32_t name_capacity,
    Inode* inode
);

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
 * return Success with zero bytes at EOF. Direct, single-indirect, and
 * double-indirect data blocks are supported; larger files return Unsupported.
 */
Status read_file(
    const FileSystem* file_system,
    const Inode* inode,
    uint64_t offset,
    uint32_t length,
    void* buffer,
    uint32_t* bytes_read
);

/**
 * Write bytes to a regular file inode, allocating direct or indirect blocks as needed.
 * Writes are synchronous and extend files with zero-filled gaps; directory mutation and
 * writes beyond the supported direct/double-indirect capacity return Unsupported.
 */
Status write_file(
    const FileSystem* file_system,
    const Inode* inode,
    uint64_t offset,
    uint32_t length,
    const void* buffer,
    uint32_t* bytes_written
);

/** Create a regular file or directory entry below a directory inode. */
Status create(const FileSystem* file_system, const Inode* parent, const char* name, bool directory, Inode* inode);

/** Remove a regular file entry below a directory inode. */
Status unlink(const FileSystem* file_system, const Inode* parent, const char* name);

/** Remove an empty directory entry below a directory inode. */
Status remove_directory(const FileSystem* file_system, const Inode* parent, const char* name);

} // namespace ext2
