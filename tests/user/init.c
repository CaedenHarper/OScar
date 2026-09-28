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
    static const char kTerminalPath[] = "/bin/terminal";
    static const char kTerminalFailure[] = "init: could not start terminal.\n";
    static const char kTerminalExit[] = "init: terminal exited.\n";
    static const char kExitMessage[] = "init: exiting.\n";
    int64_t terminal_status = 0;
    (void)write_text(kStartedMessage);
    const int64_t terminal_id = oscar_spawn(kTerminalPath);
    if(terminal_id < 0 || oscar_waitpid((uint64_t)terminal_id, &terminal_status) < 0) {
        (void)write_text(kTerminalFailure);
        exit_process();
    }
    (void)write_text(kTerminalExit);
    (void)write_text(kExitMessage);
    exit_process();
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//           cert-dcl37-c, cert-dcl51-cpp, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
