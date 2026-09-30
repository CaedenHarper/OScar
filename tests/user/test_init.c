#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-magic-numbers)

static const int64_t kKilledStatus = 137;

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

// NOLINTNEXTLINE(misc-use-internal-linkage) ELF entry point required by linker
void _start(void) {
    static const char kStartedMessage[] = "test init: user-space test suite started.\n";
    static const char kShellCommandTestPath[] = "/bin/shell_commands";
    static const char kShellCommandTestFailure[] = "test init: shell command test failed.\n";
    static const char kShellCommandTestPassed[] = "test init: shell command test passed.\n";
    static const char kArgumentTestPath[] = "/bin/argv_test";
    static const char kArgumentTestFailure[] = "test init: argv/descriptor test failed.\n";
    static const char kArgumentTestPassed[] = "test init: argv/descriptor test passed.\n";
    static const char kKillTestPath[] = "/bin/kill_target";
    static const char kKillTestFailure[] = "test init: cross-process kill test failed.\n";
    static const char kKillTestPassed[] = "test init: cross-process kill test passed.\n";
    static const char kPassedMessage[] = "OSCAR TESTS PASSED\n";
    int64_t status = 0;
    (void)write_text(kStartedMessage);

    const int64_t shell_command_test_id = oscar_spawn(kShellCommandTestPath);
    if(shell_command_test_id < 0 || oscar_waitpid((uint64_t)shell_command_test_id, &status) < 0 ||
       status != kKilledStatus) {
        (void)write_text(kShellCommandTestFailure);
        exit_process();
    }
    (void)write_text(kShellCommandTestPassed);

    const int64_t inherited_descriptor = oscar_open("/hello.txt", OSCAR_OPEN_READ);
    const char* arguments[] = {"argv_test", "alpha", "beta", 0};
    const int64_t argument_test_id = oscar_spawn_args(kArgumentTestPath, arguments);
    if(inherited_descriptor != 3 || argument_test_id < 0 || oscar_waitpid((uint64_t)argument_test_id, &status) < 0 ||
       status != 0) {
        (void)write_text(kArgumentTestFailure);
        exit_process();
    }
    (void)oscar_close(inherited_descriptor);
    (void)write_text(kArgumentTestPassed);

    const int64_t kill_test_id = oscar_spawn(kKillTestPath);
    if(kill_test_id < 0 || oscar_kill((uint64_t)kill_test_id) < 0 ||
       oscar_waitpid((uint64_t)kill_test_id, &status) < 0 || status != kKilledStatus) {
        (void)write_text(kKillTestFailure);
        exit_process();
    }
    (void)write_text(kKillTestPassed);
    (void)write_text(kPassedMessage);
    oscar_test_complete();
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-magic-numbers)
