#pragma once

#include <stddef.h>
#include <sys/types.h>

/** Read up to count bytes from a descriptor. */
ssize_t read(int descriptor, void* buffer, size_t count);

/** Write up to count bytes to a descriptor. */
ssize_t write(int descriptor, const void* buffer, size_t count);

/** Close a descriptor. */
int close(int descriptor);

/** Move a descriptor's file offset. */
off_t lseek(int descriptor, off_t offset, int whence);

/** Change the calling process's working directory. */
int chdir(const char* path);

/** Return the current working directory. */
char* getcwd(char* buffer, size_t size);

/** Remove a file or an empty directory. */
int unlink(const char* path);

/** Remove an empty directory. */
int rmdir(const char* path);

/** Create a directory. Mode is currently accepted but not enforced. */
int mkdir(const char* path, mode_t mode);

/** Return the calling process identifier. */
pid_t getpid(void);

/** Sleep for whole seconds using the kernel's 100 Hz timer. */
unsigned sleep(unsigned seconds);

/** Suspend execution for the requested number of microseconds. */
int usleep(unsigned microseconds);

/** Duplicate a descriptor. */
int dup(int descriptor);

/** Duplicate a descriptor into a requested descriptor number. */
int dup2(int descriptor, int target);

/** Create a pipe and return its read and write descriptors. */
int pipe(int descriptors[2]);
