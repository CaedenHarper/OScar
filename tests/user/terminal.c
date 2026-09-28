#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

enum {
    kSleepSystemCall = 3,
    kReadSystemCall = 7,
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
    static char line[kLineCapacity];

    // Let boot-time scheduler smoke threads finish before placing the interactive
    // prompt at the bottom of the startup log. The delay covers their bounded timer
    // and waiting tests without adding a kernel-only synchronization mechanism to
    // this temporary user-space fixture.
    syscall_sleep(kStartupDelayTicks);
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
