#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//             cert-dcl37-c, cert-dcl51-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)

static uint64_t write_text(const char* text) {
    uint64_t length = 0;
    while(text[length] != '\0') {
        ++length;
    }
    return (uint64_t)oscar_write(1, text, length);
}

__attribute__((noreturn)) static void exit_process(void) {
    oscar_exit(0);
}

void _start(void) {
    static const char kStartedMessage[] = "init: user-space initialization started.\n";
    static const char kShellCommandTestPath[] = "/bin/shell_commands";
    static const char kShellCommandTestFailure[] = "init: shell command test failed.\n";
    static const char kShellCommandTestPassed[] = "init: shell command test passed.\n";
    static const char kShellPath[] = "/bin/cash";
    static const char kShellFailure[] = "init: could not start cash.\n";
    static const char kShellExit[] = "init: cash exited.\n";
    static const char kExitMessage[] = "init: exiting.\n";
    int64_t terminal_status = 0;
    int64_t shell_command_test_status = 0;
    (void)write_text(kStartedMessage);
    const int64_t shell_command_test_id = oscar_spawn(kShellCommandTestPath);
    if(shell_command_test_id < 0 || oscar_waitpid((uint64_t)shell_command_test_id, &shell_command_test_status) < 0 ||
       shell_command_test_status != 137) {
        (void)write_text(kShellCommandTestFailure);
        exit_process();
    }
    (void)write_text(kShellCommandTestPassed);
    const int64_t shell_id = oscar_spawn(kShellPath);
    if(shell_id < 0 || oscar_waitpid((uint64_t)shell_id, &terminal_status) < 0) {
        (void)write_text(kShellFailure);
        exit_process();
    }
    (void)write_text(kShellExit);
    (void)write_text(kExitMessage);
    exit_process();
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//           cert-dcl37-c, cert-dcl51-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
