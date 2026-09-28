#pragma once

#include <stdint.h>

/** Write up to length bytes to a standard output or error descriptor. */
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

/** Open a mounted filesystem path read-only and return its descriptor. */
int64_t oscar_open(const char* path);

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
