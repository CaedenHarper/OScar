#pragma once

#include "block_device.hpp"

#include <stdint.h>

namespace vfs {

enum class Status : uint8_t {
    Success,
    InvalidArgument,
    NotMounted,
    NotFound,
    Exists,
    NotDirectory,
    NotEmpty,
    NoSpace,
    IsDirectory,
    IoError,
    Unsupported,
    ReadOnly,
    PermissionDenied,
};

enum class NodeType : uint8_t {
    Regular,
    Directory,
};

constexpr uint32_t kMaximumNameLength = 255;
constexpr uint32_t kFilesystemNameCapacity = 16;
constexpr uint32_t kDeviceNameCapacity = 32;
constexpr uint32_t kMountPointCapacity = 32;

constexpr uint32_t kOpenRead = 1;
constexpr uint32_t kOpenWrite = 2;
constexpr uint32_t kOpenReadWrite = kOpenRead | kOpenWrite;

struct Node {
    uint64_t identifier;
    uint64_t size;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    NodeType type;
};

struct Credentials {
    uint32_t uid;
    uint32_t gid;
};

constexpr uint8_t kAccessRead = 4;
constexpr uint8_t kAccessWrite = 2;
constexpr uint8_t kAccessExecute = 1;

struct DirectoryEntry {
    char name[kMaximumNameLength + 1];
    Node node;
};

struct File {
    Node node;
    uint64_t offset;
    uint32_t flags;
    bool open;
};

struct FileSystemStatus {
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    char filesystem[kFilesystemNameCapacity];
    char device[kDeviceNameCapacity];
    char mount_point[kMountPointCapacity];
};

/** Mount the ext2 filesystem as the single VFS root. Regular files may be written. */
bool mount_root(block_device::Device* device);

/** Return whether the VFS root filesystem has been mounted. */
bool is_mounted();

/** Return capacity counters and descriptive information for the mounted root filesystem. */
Status statfs(FileSystemStatus* status);

/** Resolve a path through the mounted root filesystem without opening it. */
Status resolve(const char* path, Node* node);

/** Resolve a path using the supplied credentials for directory traversal. */
Status resolve_as(const char* path, Node* node, Credentials credentials);

/** Enumerate one non-dot entry from a directory path by zero-based index. */
Status read_directory(const char* path, uint32_t index, DirectoryEntry* entry);

/** Enumerate a directory after checking read and search permissions. */
Status read_directory_as(const char* path, uint32_t index, DirectoryEntry* entry, Credentials credentials);

/** Open a path for reading into caller-owned file-handle storage. */
Status open(const char* path, uint32_t flags, File* file);

/** Open a path after checking the requested access against its inode metadata. */
Status open_as(const char* path, uint32_t flags, File* file, Credentials credentials);

/** Read from an open file and advance its current offset. */
Status read(File* file, void* buffer, uint32_t length, uint32_t* bytes_read);

/** Write to an open writable regular file and advance its current offset; directory writes are rejected. */
Status write(File* file, const void* buffer, uint32_t length, uint32_t* bytes_written);

/** Create a regular file at path and return it as an open node-like handle. */
Status create(const char* path, Node* node);

/** Create a regular file after checking write and search permission on its parent. */
Status create_as(const char* path, Node* node, Credentials credentials);

/** Create a directory at path. */
Status mkdir(const char* path);

/** Create a directory after checking write and search permission on its parent. */
Status mkdir_as(const char* path, Credentials credentials);

/** Remove a regular file at path. */
Status unlink(const char* path);

/** Remove a regular file after checking write and search permission on its parent. */
Status unlink_as(const char* path, Credentials credentials);

/** Remove an empty directory at path. */
Status rmdir(const char* path);

/** Remove an empty directory after checking write and search permission on its parent. */
Status rmdir_as(const char* path, Credentials credentials);

/** Set the current offset of an open file. */
Status seek(File* file, uint64_t offset);

/** Close a caller-owned file handle. */
Status close(File* file);

} // namespace vfs
