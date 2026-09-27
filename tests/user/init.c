#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//             cert-dcl37-c, cert-dcl51-cpp)

static uint64_t write_text(const char* text) {
    uint64_t length = 0;
    while(text[length] != '\0') {
        ++length;
    }
    uint64_t call = 0;
    __asm__ volatile("int $0x80" : "+a"(call) : "D"(text), "S"(length) : "rcx", "r11", "memory");
    return call;
}

__attribute__((noreturn)) static void exit_process(void) {
    uint64_t call = 1;
    __asm__ volatile("int $0x80" : "+a"(call) : : "rcx", "r11", "memory");
    for(;;) {
        __asm__ volatile("pause");
    }
}

void _start(void) {
    static const char kStartedMessage[] = "init: user-space initialization started.\n";
    static const char kExitMessage[] = "init: exiting.\n";
    (void)write_text(kStartedMessage);
    (void)write_text(kExitMessage);
    exit_process();
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, bugprone-reserved-identifier,
//           cert-dcl37-c, cert-dcl51-cpp)
