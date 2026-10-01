#pragma once

#include <stdint.h>

enum {
    OSCAR_ERROR_NOT_FOUND = -3,
    OSCAR_ERROR_PERMISSION_DENIED = -12,
};

enum {
    OSCAR_PROCESS_NEW = 0,
    OSCAR_PROCESS_RUNNING = 1,
    OSCAR_PROCESS_TERMINATED = 2,
};

enum {
    OSCAR_MAX_IMAGE_PATH_LENGTH = 63,
};

struct oscar_process_info {
    uint64_t id;
    uint32_t state;
    uint32_t thread_count;
    uint64_t user_page_count;
    char image_path[OSCAR_MAX_IMAGE_PATH_LENGTH + 1]; // leave room for null terminator
};

/** Open a path for reading, writing, or both; returns a descriptor or a negative error. */
int64_t oscar_open(const char* path, uint32_t flags);

/** Create an empty regular file at path. */
int64_t oscar_create(const char* path);

/** Create a directory at path. */
int64_t oscar_mkdir(const char* path);

/** Remove a regular file at path. */
int64_t oscar_unlink(const char* path);

/** Remove an empty directory at path. */
int64_t oscar_rmdir(const char* path);

enum {
    OSCAR_NODE_REGULAR = 0,
    OSCAR_NODE_DIRECTORY = 1,
    OSCAR_MAX_NAME_LENGTH = 255,
    OSCAR_FILESYSTEM_NAME_CAPACITY = 16,
    OSCAR_DEVICE_NAME_CAPACITY = 32,
    OSCAR_MOUNT_POINT_CAPACITY = 32,
};

struct oscar_stat {
    uint64_t size;
    uint32_t type;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t reserved;
};

struct oscar_statfs {
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    char filesystem[OSCAR_FILESYSTEM_NAME_CAPACITY];
    char device[OSCAR_DEVICE_NAME_CAPACITY];
    char mount_point[OSCAR_MOUNT_POINT_CAPACITY];
};

struct oscar_dirent {
    uint64_t identifier;
    uint64_t size;
    uint32_t type;
    uint32_t name_length;
    char name[OSCAR_MAX_NAME_LENGTH + 1];
};

/** Change the calling process's working directory. */
int64_t oscar_chdir(const char* path);

/** Copy the working directory into buffer and return its length without the terminator. */
int64_t oscar_getcwd(char* buffer, uint64_t length);

/** Read metadata for a path into caller-owned storage. */
int64_t oscar_stat(const char* path, struct oscar_stat* status);

/** Read capacity and mount information for the root filesystem. */
int64_t oscar_statfs(struct oscar_statfs* status);

/** Read one non-dot directory entry by zero-based index. */
int64_t oscar_readdir(const char* path, uint64_t index, struct oscar_dirent* entry);

enum {
    OSCAR_OPEN_READ = 1,
    OSCAR_OPEN_WRITE = 2,
};

/** Write up to length bytes to a descriptor. */
int64_t oscar_write(int64_t descriptor, const void* buffer, uint64_t length);

/** Terminate the calling process with status and never return. */
__attribute__((noreturn)) void oscar_exit(int64_t status);

/** Yield the processor to another runnable thread. */
void oscar_yield(void);

/** Put the calling thread to sleep for the specified timer ticks. */
void oscar_sleep(uint64_t ticks);

/** Return the calling process identifier, or a negative error code. */
int64_t oscar_getpid(void);

/** Return the calling thread identifier, or a negative error code. */
int64_t oscar_getid(void);

/** Copy one zero-based process snapshot into info, or return OSCAR_ERROR_NOT_FOUND at the end. */
int64_t oscar_get_process_info(uint64_t index, struct oscar_process_info* info);

/** Terminate the process identified by process_id, or return an error for an invalid target. */
int64_t oscar_kill(uint64_t process_id);

/** Read up to length bytes from descriptor into buffer. */
int64_t oscar_read(int64_t descriptor, void* buffer, uint64_t length);

/** Set a file descriptor's read offset. */
int64_t oscar_seek(int64_t descriptor, uint64_t offset);

/** Close a file descriptor. */
int64_t oscar_close(int64_t descriptor);

/** Duplicate a descriptor into the lowest available descriptor slot. */
int64_t oscar_dup(int64_t descriptor);

/** Duplicate a descriptor into target, closing target first when necessary. */
int64_t oscar_dup2(int64_t descriptor, int64_t target);

/** Create a pipe and write its read and write descriptors into descriptors[0] and descriptors[1]. */
int64_t oscar_pipe(int64_t descriptors[2]);

/** Create a child process by loading a filesystem-backed ELF path without arguments. */
int64_t oscar_spawn(const char* path);

/** Create a child process and pass a null-terminated argument vector to its entry point. */
int64_t oscar_spawn_args(const char* path, const char* const arguments[]);

/** Wait for the exact child process and optionally receive its exit status. */
int64_t oscar_waitpid(uint64_t process_id, int64_t* status);

/** Complete the kernel test suite and terminate the test emulator; test runner use only. */
__attribute__((noreturn)) void oscar_test_complete(void);
