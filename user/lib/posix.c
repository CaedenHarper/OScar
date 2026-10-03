#include <errno.h>
#include <fcntl.h>
#include <oscar/syscalls.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    kTimerFrequency = 100,
    kSeekSet = 0,
    kAccessModeMask = 3,
};

static int failure(int64_t result) {
    switch(result) {
        case OSCAR_ERROR_NOT_FOUND:
            errno = ENOENT;
            break;
        case OSCAR_ERROR_PERMISSION_DENIED:
            errno = EACCES;
            break;
        case OSCAR_ERROR_NOT_CONNECTED:
            errno = ENOTCONN;
            break;
        default:
            errno = EIO;
            break;
    }
    return -1;
}

int open(const char* path, int flags, ...) {
    uint32_t oscar_flags = OSCAR_OPEN_READ;
    const unsigned access_mode = (unsigned)flags & (unsigned)kAccessModeMask;
    if(access_mode == O_WRONLY) {
        oscar_flags = OSCAR_OPEN_WRITE;
    } else if(access_mode == O_RDWR) {
        oscar_flags = (uint32_t)OSCAR_OPEN_READ | (uint32_t)OSCAR_OPEN_WRITE;
    }
    int64_t descriptor = oscar_open(path, oscar_flags);
    if(descriptor < 0 && ((unsigned)flags & (unsigned)O_CREAT) != 0 && oscar_create(path) == 0) {
        descriptor = oscar_open(path, oscar_flags);
    }
    if(descriptor < 0) {
        return failure(descriptor);
    }
    return (int)descriptor;
}

ssize_t read(int descriptor, void* buffer, size_t count) {
    const int64_t result = oscar_read(descriptor, buffer, count);
    return result < 0 ? (ssize_t)failure(result) : (ssize_t)result;
}

ssize_t write(int descriptor, const void* buffer, size_t count) {
    const int64_t result = oscar_write(descriptor, buffer, count);
    return result < 0 ? (ssize_t)failure(result) : (ssize_t)result;
}

int close(int descriptor) {
    const int64_t result = oscar_close(descriptor);
    return result < 0 ? failure(result) : 0;
}

off_t lseek(int descriptor, off_t offset, int whence) {
    if(whence != kSeekSet || offset < 0) {
        errno = EINVAL;
        return -1;
    }
    const int64_t result = oscar_seek(descriptor, (uint64_t)offset);
    return result < 0 ? (off_t)failure(result) : offset;
}

int chdir(const char* path) {
    const int64_t result = oscar_chdir(path);
    return result < 0 ? failure(result) : 0;
}

char* getcwd(char* buffer, size_t size) {
    const int64_t result = oscar_getcwd(buffer, size);
    if(result < 0) {
        (void)failure(result);
        return (char*)0;
    }
    return buffer;
}

int unlink(const char* path) {
    const int64_t result = oscar_unlink(path);
    return result < 0 ? failure(result) : 0;
}

int rmdir(const char* path) {
    const int64_t result = oscar_rmdir(path);
    return result < 0 ? failure(result) : 0;
}

int mkdir(const char* path, mode_t mode) {
    (void)mode;
    const int64_t result = oscar_mkdir(path);
    return result < 0 ? failure(result) : 0;
}

pid_t getpid(void) {
    const int64_t result = oscar_getpid();
    return result < 0 ? (pid_t)failure(result) : (pid_t)result;
}

unsigned sleep(unsigned seconds) {
    oscar_sleep((uint64_t)seconds * kTimerFrequency);
    return 0;
}

int usleep(unsigned microseconds) {
    const uint64_t tick_duration = 1000000 / kTimerFrequency;
    const uint64_t ticks = ((uint64_t)microseconds + tick_duration - 1) / tick_duration;
    oscar_sleep(ticks);
    return 0;
}

int dup(int descriptor) {
    const int64_t result = oscar_dup(descriptor);
    return result < 0 ? failure(result) : (int)result;
}

int dup2(int descriptor, int target) {
    const int64_t result = oscar_dup2(descriptor, target);
    return result < 0 ? failure(result) : (int)result;
}

int pipe(int descriptors[2]) {
    int64_t oscar_descriptors[2];
    const int64_t result = oscar_pipe(oscar_descriptors);
    if(result >= 0) {
        descriptors[0] = (int)oscar_descriptors[0];
        descriptors[1] = (int)oscar_descriptors[1];
    }
    return result < 0 ? failure(result) : 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) matches the POSIX kill signature.
int kill(pid_t process_id, int signal) {
    if(signal != SIGTERM) {
        errno = EINVAL;
        return -1;
    }
    const int64_t result = oscar_kill((uint64_t)process_id);
    return result < 0 ? failure(result) : 0;
}

pid_t waitpid(pid_t process_id, int* status, int options) {
    if(options != 0) {
        errno = EINVAL;
        return -1;
    }
    int64_t oscar_status = 0;
    const int64_t result = oscar_waitpid((uint64_t)process_id, &oscar_status);
    if(result < 0) {
        return (pid_t)failure(result);
    }
    if(status != (int*)0) {
        *status = (int)oscar_status;
    }
    return process_id;
}

int stat(const char* path, struct stat* status) {
    struct oscar_stat oscar_status;
    const int64_t result = oscar_stat(path, &oscar_status);
    if(result < 0) {
        return failure(result);
    }
    status->st_size = (off_t)oscar_status.size;
    status->st_mode = oscar_status.type == OSCAR_NODE_DIRECTORY ? S_IFDIR : S_IFREG;
    status->st_mode |= oscar_status.mode;
    status->st_uid = oscar_status.uid;
    status->st_gid = oscar_status.gid;
    return 0;
}
