#pragma once

#include "block_device.hpp"

#include <stdint.h>

namespace vfs {

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    NotMounted,
    NotFound,
    NotDirectory,
    IsDirectory,
    IoError,
    Unsupported,
    ReadOnly,
};

enum class NodeType : uint8_t {
    Regular,
    Directory,
};

constexpr uint32_t kOpenRead = 1;
constexpr uint32_t kOpenWrite = 2;
constexpr uint32_t kOpenReadWrite = kOpenRead | kOpenWrite;

struct Node {
    uint64_t identifier;
    uint64_t size;
    NodeType type;
};

struct File {
    Node node;
    uint64_t offset;
    uint32_t flags;
    bool open;
};

/** Mount the ext2 filesystem as the single VFS root. Regular files may be written. */
bool mount_root(block_device::Device* device);

/** Return whether the VFS root filesystem has been mounted. */
bool is_mounted();

/** Resolve a path through the mounted root filesystem without opening it. */
Status resolve(const char* path, Node* node);

/** Open a path for reading into caller-owned file-handle storage. */
Status open(const char* path, uint32_t flags, File* file);

/** Read from an open file and advance its current offset. */
Status read(File* file, void* buffer, uint32_t length, uint32_t* bytes_read);

/** Write to an open writable regular file and advance its current offset; directory writes are rejected. */
Status write(File* file, const void* buffer, uint32_t length, uint32_t* bytes_written);

/** Set the current offset of an open file. */
Status seek(File* file, uint64_t offset);

/** Close a caller-owned file handle. */
Status close(File* file);

} // namespace vfs
