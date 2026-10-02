#include <oscar/syscalls.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-magic-numbers)

static const int64_t kKilledStatus = 137;
static const int64_t kPipeTargetDescriptor = 7;
static const uint64_t kPingTimeoutMilliseconds = 200;

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
    static const char kPipeTestFailure[] = "test init: pipe/descriptor duplication test failed.\n";
    static const char kPipeTestPassed[] = "test init: pipe/descriptor duplication test passed.\n";
    static const char kKillTestPath[] = "/bin/kill_target";
    static const char kKillTestFailure[] = "test init: cross-process kill test failed.\n";
    static const char kKillTestPassed[] = "test init: cross-process kill test passed.\n";
    static const char kFilesystemStatusFailure[] = "test init: filesystem status command test failed.\n";
    static const char kFilesystemStatusPassed[] = "test init: filesystem status commands passed.\n";
    static const char kNetworkFailure[] = "test init: network ping test failed.\n";
    static const char kNetworkPassed[] = "test init: network ping test passed.\n";
    static const char kSocketFailure[] = "test init: socket syscall test failed.\n";
    static const char kSocketPassed[] = "test init: socket syscall test passed.\n";
    static const char kHttpParserPath[] = "/bin/http_parser_test";
    static const char kHttpParserFailure[] = "test init: HTTP parser test failed.\n";
    static const char kHttpParserPassed[] = "test init: HTTP parser test passed.\n";
    static const uint8_t kGatewayAddress[] = {10, 0, 2, 2};
    static const char kDfPath[] = "/bin/df";
    static const char kDuPath[] = "/bin/du";
    static const char kMountPath[] = "/bin/mount";
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

    int64_t pipe_descriptors[2] = {-1, -1};
    char pipe_message[] = "pipe ok";
    char pipe_result[sizeof(pipe_message)];
    for(uint64_t index = 0; index < sizeof(pipe_result); ++index) {
        pipe_result[index] = '\0';
    }
    const int64_t pipe_result_descriptor = oscar_pipe(pipe_descriptors);
    const int64_t duplicate_read_descriptor = pipe_result_descriptor == 0 ? oscar_dup(pipe_descriptors[0]) : -1;
    const int64_t duplicate_write_descriptor =
        pipe_result_descriptor == 0 ? oscar_dup2(pipe_descriptors[1], kPipeTargetDescriptor) : -1;
    const int64_t write_result = duplicate_write_descriptor >= 0
                                     ? oscar_write(duplicate_write_descriptor, pipe_message, sizeof(pipe_message) - 1)
                                     : -1;
    const int64_t read_result = duplicate_read_descriptor >= 0
                                    ? oscar_read(duplicate_read_descriptor, pipe_result, sizeof(pipe_result) - 1)
                                    : -1;
    int pipe_contents_match = 1;
    for(uint64_t index = 0; index < sizeof(pipe_message) - 1; ++index) {
        if(pipe_result[index] != pipe_message[index]) {
            pipe_contents_match = 0;
        }
    }
    if(pipe_result_descriptor < 0 || duplicate_read_descriptor < 0 ||
       duplicate_write_descriptor != kPipeTargetDescriptor || write_result != (int64_t)(sizeof(pipe_message) - 1) ||
       read_result != write_result || !pipe_contents_match) {
        (void)write_text(kPipeTestFailure);
        exit_process();
    }
    (void)oscar_close(pipe_descriptors[0]);
    (void)oscar_close(pipe_descriptors[1]);
    (void)oscar_close(duplicate_read_descriptor);
    (void)oscar_close(duplicate_write_descriptor);
    (void)write_text(kPipeTestPassed);

    const int64_t kill_test_id = oscar_spawn(kKillTestPath);
    if(kill_test_id < 0 || oscar_kill((uint64_t)kill_test_id) < 0 ||
       oscar_waitpid((uint64_t)kill_test_id, &status) < 0 || status != kKilledStatus) {
        (void)write_text(kKillTestFailure);
        exit_process();
    }
    (void)write_text(kKillTestPassed);

    const char* status_commands[] = {kDfPath, kDuPath, kMountPath};
    for(uint64_t index = 0; index < sizeof(status_commands) / sizeof(status_commands[0]); ++index) {
        const int64_t command_id = oscar_spawn(status_commands[index]);
        if(command_id < 0 || oscar_waitpid((uint64_t)command_id, &status) < 0 || status != 0) {
            (void)write_text(kFilesystemStatusFailure);
            exit_process();
        }
    }
    (void)write_text(kFilesystemStatusPassed);
    if(oscar_ping(kGatewayAddress, kPingTimeoutMilliseconds) < 0) {
        (void)write_text(kNetworkFailure);
        exit_process();
    }
    (void)write_text(kNetworkPassed);
    const int64_t socket_descriptor = oscar_socket(OSCAR_AF_INET, OSCAR_SOCK_STREAM, OSCAR_IPPROTO_TCP);
    const char socket_data[] = "not connected";
    if(socket_descriptor < 0 ||
       oscar_send(socket_descriptor, socket_data, sizeof(socket_data) - 1) != OSCAR_ERROR_NOT_CONNECTED ||
       oscar_close(socket_descriptor) < 0) {
        (void)write_text(kSocketFailure);
        exit_process();
    }
    (void)write_text(kSocketPassed);
    const int64_t http_parser_id = oscar_spawn(kHttpParserPath);
    if(http_parser_id < 0 || oscar_waitpid((uint64_t)http_parser_id, &status) < 0 || status != 0) {
        (void)write_text(kHttpParserFailure);
        exit_process();
    }
    (void)write_text(kHttpParserPassed);
    (void)write_text(kPassedMessage);
    oscar_test_complete();
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp, readability-magic-numbers)
