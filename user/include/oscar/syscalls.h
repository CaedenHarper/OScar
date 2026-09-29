#pragma once

#include <stdint.h>

enum {
    OSCAR_ERROR_NOT_FOUND = -3,
};

enum {
    OSCAR_PROCESS_NEW = 0,
    OSCAR_PROCESS_RUNNING = 1,
    OSCAR_PROCESS_TERMINATED = 2,
};

struct oscar_process_info {
    uint64_t id;
    uint32_t state;
    uint32_t thread_count;
    uint64_t user_page_count;
    char image_path[64];
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
};

struct oscar_stat {
    uint64_t size;
    uint32_t type;
    uint32_t reserved;
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

/** Read up to length bytes from descriptor into buffer. */
int64_t oscar_read(int64_t descriptor, void* buffer, uint64_t length);

/** Set a file descriptor's read offset. */
int64_t oscar_seek(int64_t descriptor, uint64_t offset);

/** Close a file descriptor. */
int64_t oscar_close(int64_t descriptor);

/** Create a child process by loading a filesystem-backed ELF path. */
int64_t oscar_spawn(const char* path);

/** Wait for the exact child process and optionally receive its exit status. */
int64_t oscar_waitpid(uint64_t process_id, int64_t* status);
