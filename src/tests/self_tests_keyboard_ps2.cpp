#include "keyboard.hpp"
#include "keyboard_ps2.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "terminal.hpp"

#include <stdint.h>

namespace self_tests_detail {

void test_keyboard_ps2() {
    if(!keyboard_ps2::initialize()) {
        serial::write("PS/2 keyboard unavailable; continuing without keyboard input.\n");
        return;
    }

    keyboard::Event event = {};
    if(!keyboard::is_available() || keyboard::pending_events() != 0 || keyboard::poll(&event)) {
        panic::halt("PS/2 keyboard smoke test found unexpected initial input");
    }
    serial::write("PS/2 keyboard driver smoke test passed.\n");
}

void test_terminal() {
    if(!terminal::is_available()) {
        panic::halt("terminal smoke test found an unavailable terminal");
    }

    keyboard::submit_event(
        {.key = keyboard::Key::Character,
         .character = 'o',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Character,
         .character = 'k',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Enter,
         .character = '\n',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );

    char line[4] = {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const int64_t length = terminal::read(line, sizeof(line));
    if(length != 3 || line[0] != 'o' || line[1] != 'k' || line[2] != '\n') {
        panic::halt("terminal smoke test did not produce the expected input line");
    }

    keyboard::submit_event(
        {.key = keyboard::Key::Character,
         .character = 'a',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Character,
         .character = 'b',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::ArrowLeft,
         .character = 0,
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Character,
         .character = 'X',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Delete, .character = 0, .pressed = true, .shift = false, .control = false, .alt = false}
    );
    keyboard::submit_event(
        {.key = keyboard::Key::Enter,
         .character = '\n',
         .pressed = true,
         .shift = false,
         .control = false,
         .alt = false}
    );

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    const int64_t edited_length = terminal::read(line, sizeof(line));
    if(edited_length != 3 || line[0] != 'a' || line[1] != 'X' || line[2] != '\n') {
        panic::halt("terminal cursor editing smoke test did not produce the expected line");
    }
    serial::write("Terminal line-discipline smoke test passed.\n");
}

} // namespace self_tests_detail
