#include <oscar/syscalls.h>
#include <stdint.h>

enum {
    kWriteSystemCall = 0,
    kExitSystemCall = 1,
    kYieldSystemCall = 2,
    kSleepSystemCall = 3,
    kGetPidSystemCall = 4,
    kGetIdSystemCall = 5,
    kGetProcessInfoSystemCall = 20,
    kKillSystemCall = 21,
    kOpenSystemCall = 6,
    kReadSystemCall = 7,
    kCloseSystemCall = 8,
    kSeekSystemCall = 9,
    kSpawnSystemCall = 10,
    kWaitPidSystemCall = 11,
    kTestCompleteSystemCall = 22,
    kCreateSystemCall = 12,
    kMkdirSystemCall = 13,
    kUnlinkSystemCall = 14,
    kRmdirSystemCall = 15,
    kChdirSystemCall = 16,
    kGetcwdSystemCall = 17,
    kStatSystemCall = 18,
    kReaddirSystemCall = 19,
    kDupSystemCall = 23,
    kDup2SystemCall = 24,
    kPipeSystemCall = 25,
};

int64_t oscar_write(int64_t descriptor, const void* buffer, uint64_t length) {
    uint64_t call = kWriteSystemCall;
    __asm__ volatile("int $0x80"
                     : "+a"(call)
                     : "D"((uint64_t)descriptor), "S"(buffer), "d"(length)
                     : "rcx", "r11", "memory");
    return (int64_t)call;
}

__attribute__((noreturn)) void oscar_exit(int64_t status) {
    uint64_t call = kExitSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"((uint64_t)status) : "rcx", "r11", "memory");
    for(;;) {
        __asm__ volatile("pause");
    }
}

void oscar_yield(void) {
    uint64_t call = kYieldSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
}

void oscar_sleep(uint64_t ticks) {
    uint64_t call = kSleepSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(ticks) : "rcx", "r11", "memory");
}

int64_t oscar_getpid(void) {
    uint64_t call = kGetPidSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_getid(void) {
    uint64_t call = kGetIdSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the syscall writes the process snapshot.
int64_t oscar_get_process_info(uint64_t index, struct oscar_process_info* info) {
    uint64_t call = kGetProcessInfoSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(index), "S"(info) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_kill(uint64_t process_id) {
    uint64_t call = kKillSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(process_id) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_open(const char* path, uint32_t flags) {
    uint64_t call = kOpenSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path), "S"((uint64_t)flags) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_create(const char* path) {
    uint64_t call = kCreateSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_mkdir(const char* path) {
    uint64_t call = kMkdirSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_unlink(const char* path) {
    uint64_t call = kUnlinkSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_rmdir(const char* path) {
    uint64_t call = kRmdirSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_chdir(const char* path) {
    uint64_t call = kChdirSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the kernel writes the working directory into buffer.
int64_t oscar_getcwd(char* buffer, uint64_t length) {
    uint64_t call = kGetcwdSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(buffer), "S"(length) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_stat(const char* path, struct oscar_stat* status) {
    uint64_t call = kStatSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path), "S"(status) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_readdir(const char* path, uint64_t index, struct oscar_dirent* entry) {
    uint64_t call = kReaddirSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path), "S"(index), "d"(entry) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_read(int64_t descriptor, void* buffer, uint64_t length) {
    uint64_t call = kReadSystemCall;
    __asm__ volatile("int $0x80"
                     : "+a"(call)
                     : "D"((uint64_t)descriptor), "S"(buffer), "d"(length)
                     : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) descriptor and offset use distinct signedness by ABI convention
int64_t oscar_seek(int64_t descriptor, uint64_t offset) {
    uint64_t call = kSeekSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"((uint64_t)descriptor), "S"(offset) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_close(int64_t descriptor) {
    uint64_t call = kCloseSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"((uint64_t)descriptor) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_dup(int64_t descriptor) {
    uint64_t call = kDupSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"((uint64_t)descriptor) : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) syscall ABI uses old descriptor then target.
int64_t oscar_dup2(int64_t descriptor, int64_t target) {
    uint64_t call = kDup2SystemCall;
    __asm__ volatile("int $0x80"
                     : "+a"(call)
                     : "D"((uint64_t)descriptor), "S"((uint64_t)target)
                     : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the syscall writes both descriptors.
int64_t oscar_pipe(int64_t descriptors[2]) {
    uint64_t call = kPipeSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(descriptors) : "rcx", "r11", "memory");
    return (int64_t)call;
}

int64_t oscar_spawn(const char* path) {
    return oscar_spawn_args(path, (const char* const*)0);
}

int64_t oscar_spawn_args(const char* path, const char* const arguments[]) {
    uint64_t call = kSpawnSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path), "S"(arguments) : "rcx", "r11", "memory");
    return (int64_t)call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the syscall writes the child status
int64_t oscar_waitpid(uint64_t process_id, int64_t* status) {
    uint64_t call = kWaitPidSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(process_id), "S"(status) : "rcx", "r11", "memory");
    return (int64_t)call;
}

__attribute__((noreturn)) void oscar_test_complete(void) {
    uint64_t call = kTestCompleteSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
    for(;;) {
        __asm__ volatile("pause");
    }
}
