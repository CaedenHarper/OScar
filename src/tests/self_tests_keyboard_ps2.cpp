#include "keyboard_ps2.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"

namespace self_tests_detail {

void test_keyboard_ps2() {
    if(!keyboard_ps2::initialize()) {
        serial::write("PS/2 keyboard unavailable; continuing without keyboard input.\n");
        return;
    }

    keyboard_ps2::KeyEvent event = {};
    if(!keyboard_ps2::is_available() || keyboard_ps2::pending_events() != 0 || keyboard_ps2::poll(&event)) {
        panic::halt("PS/2 keyboard smoke test found unexpected initial input");
    }
    serial::write("PS/2 keyboard driver smoke test passed.\n");
}

} // namespace self_tests_detail
