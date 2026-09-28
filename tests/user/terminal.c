#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)

enum {
    kLineCapacity = 128,
    kStartupDelayTicks = 32,
};

__attribute__((noreturn)) static void syscall_exit(void) {
    oscar_exit(0);
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
    oscar_sleep(kStartupDelayTicks);
    const int64_t child_id = oscar_spawn(child_path);
    if(child_id < 0 || oscar_waitpid((uint64_t)child_id, &child_status) < 0 || child_status != 0) {
        (void)oscar_write(1, spawn_failure, sizeof(spawn_failure) - 1);
        syscall_exit();
    }
    (void)oscar_write(1, spawn_success, sizeof(spawn_success) - 1);
    for(;;) {
        (void)oscar_write(1, prompt, sizeof(prompt) - 1);
        const int64_t count = oscar_read(0, line, sizeof(line));
        if(count < 0) {
            (void)oscar_write(1, failure, sizeof(failure) - 1);
            syscall_exit();
        }
        (void)oscar_write(1, prefix, sizeof(prefix) - 1);
        (void)oscar_write(1, line, (uint64_t)count);
    }
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
