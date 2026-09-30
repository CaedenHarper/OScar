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
    static const char kArgumentTestPath[] = "/bin/argv_test";
    static const char kArgumentTestFailure[] = "init: argv/descriptor test failed.\n";
    static const char kArgumentTestPassed[] = "init: argv/descriptor test passed.\n";
    static const char kKillTestPath[] = "/bin/kill_target";
    static const char kKillTestFailure[] = "init: cross-process kill test failed.\n";
    static const char kKillTestPassed[] = "init: cross-process kill test passed.\n";
    static const char kShellPath[] = "/bin/cash";
    static const char kShellFailure[] = "init: could not start cash.\n";
    static const char kShellExit[] = "init: cash exited.\n";
    static const char kExitMessage[] = "init: exiting.\n";
    int64_t terminal_status = 0;
    int64_t shell_command_test_status = 0;
    int64_t argument_test_status = 0;
    int64_t kill_test_status = 0;
    (void)write_text(kStartedMessage);
    const int64_t shell_command_test_id = oscar_spawn(kShellCommandTestPath);
    if(shell_command_test_id < 0 || oscar_waitpid((uint64_t)shell_command_test_id, &shell_command_test_status) < 0 ||
       shell_command_test_status != 137) {
        (void)write_text(kShellCommandTestFailure);
        exit_process();
    }
    (void)write_text(kShellCommandTestPassed);
    const int64_t inherited_descriptor = oscar_open("/hello.txt", OSCAR_OPEN_READ);
    const char* argument_test_arguments[] = {"argv_test", "alpha", "beta", 0};
    const int64_t argument_test_id = oscar_spawn_args(kArgumentTestPath, argument_test_arguments);
    if(inherited_descriptor != 3 || argument_test_id < 0 ||
       oscar_waitpid((uint64_t)argument_test_id, &argument_test_status) < 0 || argument_test_status != 0) {
        (void)write_text(kArgumentTestFailure);
        exit_process();
    }
    (void)oscar_close(inherited_descriptor);
    (void)write_text(kArgumentTestPassed);
    const int64_t kill_test_id = oscar_spawn(kKillTestPath);
    if(kill_test_id < 0 || oscar_kill((uint64_t)kill_test_id) < 0 ||
       oscar_waitpid((uint64_t)kill_test_id, &kill_test_status) < 0 || kill_test_status != 137) {
        (void)write_text(kKillTestFailure);
        exit_process();
    }
    (void)write_text(kKillTestPassed);
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
