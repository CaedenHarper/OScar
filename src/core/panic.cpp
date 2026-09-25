#include "panic.hpp"

#include "serial.hpp"

namespace panic {

[[noreturn]] void halt(const char* message) {
    serial::write("PANIC: ");
    serial::write(message);
    serial::write("\n");

    // Halt instead of spinning so a panic cannot continue consuming CPU while interrupts
    // are disabled and so the final serial diagnostic remains stable for QEMU inspection.
    for(;;) {
        asm volatile("cli; hlt");
    }
}

} // namespace panic
