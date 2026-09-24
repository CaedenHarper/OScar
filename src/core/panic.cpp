#include "panic.hpp"

#include "serial.hpp"

namespace panic {

[[noreturn]] void halt(const char* message) {
    serial::write("PANIC: ");
    serial::write(message);
    serial::write("\n");

    for(;;) {
        asm volatile("cli; hlt");
    }
}

} // namespace panic
