#include <fcntl.h>
#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const int kKilledStatus = 137;
static const int kPipeTargetDescriptor = 7;
static const uint64_t kPingTimeoutMilliseconds = 200;

__attribute__((noreturn)) static void exit_process(void) {
    _Exit(0);
}

static int run_child(const char* path, int* status) {
    const int64_t child_id = oscar_spawn(path);
    return child_id >= 0 && waitpid((pid_t)child_id, status, 0) >= 0 && *status == 0;
}

int main(void) {
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
    static const char kLibcTestPath[] = "/bin/libc_test";
    static const char kLibcTestFailure[] = "test init: C library test failed.\n";
    static const char kLibcTestPassed[] = "test init: C library test passed.\n";
    static const uint8_t kGatewayAddress[] = {10, 0, 2, 2};
    static const char kDfPath[] = "/bin/df";
    static const char kDuPath[] = "/bin/du";
    static const char kMountPath[] = "/bin/mount";
    static const char kPassedMessage[] = "OSCAR TESTS PASSED\n";
    int status = 0;
    oscar_write_string(kStartedMessage);

    const int64_t shell_command_test_id = oscar_spawn(kShellCommandTestPath);
    if(shell_command_test_id < 0 || waitpid((pid_t)shell_command_test_id, &status, 0) < 0 || status != kKilledStatus) {
        oscar_write_string(kShellCommandTestFailure);
        exit_process();
    }
    oscar_write_string(kShellCommandTestPassed);

    const int inherited_descriptor = open("/hello.txt", O_RDONLY);
    const char* arguments[] = {"argv_test", "alpha", "beta", 0};
    const int64_t argument_test_id = oscar_spawn_args(kArgumentTestPath, arguments);
    if(inherited_descriptor != 3 || argument_test_id < 0 || waitpid((pid_t)argument_test_id, &status, 0) < 0 ||
       status != 0) {
        oscar_write_string(kArgumentTestFailure);
        exit_process();
    }
    (void)close(inherited_descriptor);
    oscar_write_string(kArgumentTestPassed);

    int pipe_descriptors[2] = {-1, -1};
    char pipe_message[] = "pipe ok";
    char pipe_result[sizeof(pipe_message)];
    for(uint64_t index = 0; index < sizeof(pipe_result); ++index) {
        pipe_result[index] = '\0';
    }
    // OScar has no pipe2 or close-on-exec flag yet; inheritance is the behavior under test here.
    // NOLINTNEXTLINE(android-cloexec-pipe)
    const int pipe_result_descriptor = pipe(pipe_descriptors);
    // NOLINTNEXTLINE(android-cloexec-dup)
    const int duplicate_read_descriptor = pipe_result_descriptor == 0 ? dup(pipe_descriptors[0]) : -1;
    const int duplicate_write_descriptor =
        pipe_result_descriptor == 0 ? dup2(pipe_descriptors[1], kPipeTargetDescriptor) : -1;
    const int64_t write_result = duplicate_write_descriptor >= 0
                                     ? write(duplicate_write_descriptor, pipe_message, sizeof(pipe_message) - 1)
                                     : -1;
    const int64_t read_result =
        duplicate_read_descriptor >= 0 ? read(duplicate_read_descriptor, pipe_result, sizeof(pipe_result) - 1) : -1;
    int pipe_contents_match = 1;
    for(uint64_t index = 0; index < sizeof(pipe_message) - 1; ++index) {
        if(pipe_result[index] != pipe_message[index]) {
            pipe_contents_match = 0;
        }
    }
    if(pipe_result_descriptor < 0 || duplicate_read_descriptor < 0 ||
       duplicate_write_descriptor != kPipeTargetDescriptor || write_result != (int64_t)(sizeof(pipe_message) - 1) ||
       read_result != write_result || !pipe_contents_match) {
        oscar_write_string(kPipeTestFailure);
        exit_process();
    }
    (void)close(pipe_descriptors[0]);
    (void)close(pipe_descriptors[1]);
    (void)close(duplicate_read_descriptor);
    (void)close(duplicate_write_descriptor);
    oscar_write_string(kPipeTestPassed);

    const int64_t kill_test_id = oscar_spawn(kKillTestPath);
    if(kill_test_id < 0 || kill((pid_t)kill_test_id, SIGTERM) < 0 || waitpid((pid_t)kill_test_id, &status, 0) < 0 ||
       status != kKilledStatus) {
        oscar_write_string(kKillTestFailure);
        exit_process();
    }
    oscar_write_string(kKillTestPassed);

    const char* status_commands[] = {kDfPath, kDuPath, kMountPath};
    for(uint64_t index = 0; index < sizeof(status_commands) / sizeof(status_commands[0]); ++index) {
        const int64_t command_id = oscar_spawn(status_commands[index]);
        if(command_id < 0 || waitpid((pid_t)command_id, &status, 0) < 0 || status != 0) {
            oscar_write_string(kFilesystemStatusFailure);
            exit_process();
        }
    }
    oscar_write_string(kFilesystemStatusPassed);
    if(oscar_ping(kGatewayAddress, kPingTimeoutMilliseconds) < 0) {
        oscar_write_string(kNetworkFailure);
        exit_process();
    }
    oscar_write_string(kNetworkPassed);
    const int64_t socket_descriptor = oscar_socket(OSCAR_AF_INET, OSCAR_SOCK_STREAM, OSCAR_IPPROTO_TCP);
    const char socket_data[] = "not connected";
    if(socket_descriptor < 0 ||
       oscar_send(socket_descriptor, socket_data, sizeof(socket_data) - 1) != OSCAR_ERROR_NOT_CONNECTED ||
       close((int)socket_descriptor) < 0) {
        oscar_write_string(kSocketFailure);
        exit_process();
    }
    oscar_write_string(kSocketPassed);
    if(!run_child(kHttpParserPath, &status)) {
        oscar_write_string(kHttpParserFailure);
        exit_process();
    }
    oscar_write_string(kHttpParserPassed);
    if(!run_child(kLibcTestPath, &status)) {
        oscar_write_string(kLibcTestFailure);
        exit_process();
    }
    oscar_write_string(kLibcTestPassed);
    oscar_write_string(kPassedMessage);
    oscar_test_complete();
}
