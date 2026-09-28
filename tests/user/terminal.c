#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

enum {
    kSleepSystemCall = 3,
    kReadSystemCall = 7,
    kSpawnSystemCall = 10,
    kWaitPidSystemCall = 11,
    kLineCapacity = 128,
    kStartupDelayTicks = 32,
};

static uint64_t syscall_write(uint64_t descriptor, const char* buffer, uint64_t length) {
    uint64_t call = 0;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(descriptor), "S"(buffer), "d"(length) : "rcx", "r11", "memory");
    return call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the syscall writes into this user buffer
static uint64_t syscall_read(uint64_t descriptor, char* buffer, uint64_t length) {
    uint64_t call = kReadSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(descriptor), "S"(buffer), "d"(length) : "rcx", "r11", "memory");
    return call;
}

static void syscall_sleep(uint64_t ticks) {
    uint64_t call = kSleepSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(ticks) : "rcx", "r11", "memory");
}

static uint64_t syscall_spawn(const char* path) {
    uint64_t call = kSpawnSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(path) : "rcx", "r11", "memory");
    return call;
}

// NOLINTNEXTLINE(readability-non-const-parameter) the syscall writes the child status
static uint64_t syscall_waitpid(uint64_t process_id, int64_t* status) {
    uint64_t call = kWaitPidSystemCall;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(process_id), "S"(status) : "rcx", "r11", "memory");
    return call;
}

__attribute__((noreturn)) static void syscall_exit(void) {
    uint64_t call = 1;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
    for(;;) {
        __asm__ volatile("pause");
    }
}

void _start(void) {
    static const char prompt[] = "terminal: type a line> ";
    static const char prefix[] = "terminal echo: ";
    static const char failure[] = "terminal: read failed.\n";
    static const char spawn_failure[] = "terminal: spawn/waitpid failed.\n";
    static const char spawn_success[] = "terminal: spawn/waitpid passed.\n";
    static const char child_path[] = "/bin/second.elf";
    static char line[kLineCapacity];
    int64_t child_status = 0;

    // Let boot-time scheduler smoke threads finish before placing the interactive
    // prompt at the bottom of the startup log. The delay covers their bounded timer
    // and waiting tests without adding a kernel-only synchronization mechanism to
    // this temporary user-space fixture.
    syscall_sleep(kStartupDelayTicks);
    const uint64_t child_id = syscall_spawn(child_path);
    if((int64_t)child_id < 0 || (int64_t)syscall_waitpid(child_id, &child_status) < 0 || child_status != 0) {
        (void)syscall_write(1, spawn_failure, sizeof(spawn_failure) - 1);
        syscall_exit();
    }
    (void)syscall_write(1, spawn_success, sizeof(spawn_success) - 1);
    for(;;) {
        (void)syscall_write(1, prompt, sizeof(prompt) - 1);
        const uint64_t count = syscall_read(0, line, sizeof(line));
        if((int64_t)count < 0) {
            (void)syscall_write(1, failure, sizeof(failure) - 1);
            syscall_exit();
        }
        (void)syscall_write(1, prefix, sizeof(prefix) - 1);
        (void)syscall_write(1, line, count);
    }
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
