#include <oscar/syscalls.h>

// `_start` is the ELF entry point supplied by the linker. It translates the
// kernel's initial process ABI into the conventional C `main` entry point and
// preserves the program's return value as its process status.
extern int main(int argc, char** argv);

// NOLINTNEXTLINE(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
void _start(int argc, char** argv) {
    oscar_exit(main(argc, argv));
}
