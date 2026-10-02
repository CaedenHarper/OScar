#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//             cert-dcl37-c, cert-dcl51-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)

__attribute__((noreturn)) static void exit_process(void) {
    oscar_exit(0);
}

void _start(void) {
    static const char kStartedMessage[] = "init: user-space initialization started.\n";
    static const char kShellPath[] = "/bin/cash";
    static const char kShellFailure[] = "init: could not start cash.\n";
    static const char kShellExit[] = "init: cash exited.\n";
    static const char kExitMessage[] = "init: exiting.\n";
    int64_t terminal_status = 0;
    oscar_write_string(kStartedMessage);
    const int64_t shell_id = oscar_spawn(kShellPath);
    if(shell_id < 0 || oscar_waitpid((uint64_t)shell_id, &terminal_status) < 0) {
        oscar_write_string(kShellFailure);
        exit_process();
    }
    oscar_write_string(kShellExit);
    oscar_write_string(kExitMessage);
    exit_process();
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//           cert-dcl37-c, cert-dcl51-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
