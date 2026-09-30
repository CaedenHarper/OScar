#include "terminal.hpp"

#include "keyboard.hpp"
#include "serial.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,
//             cppcoreguidelines-pro-bounds-constant-array-index,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace {

constexpr uint32_t kLineCapacity = 256;

char g_line[kLineCapacity] = {};
uint32_t g_line_length = 0;
uint32_t g_cursor = 0;
bool g_available = false;

void move_cursor_left(uint32_t count) {
    for(uint32_t index = 0; index < count; ++index) {
        serial::write("\x1b[D");
    }
}

void move_cursor_right(uint32_t count) {
    for(uint32_t index = 0; index < count; ++index) {
        serial::write("\x1b[C");
    }
}

void redraw_suffix() {
    for(uint32_t index = g_cursor; index < g_line_length; ++index) {
        serial::putc(g_line[index]);
    }
    serial::putc(' ');
    move_cursor_left((g_line_length - g_cursor) + 1);
}

void insert_character(char character) {
    if(g_line_length + 1 >= kLineCapacity) {
        return;
    }

    for(uint32_t index = g_line_length; index > g_cursor; --index) {
        g_line[index] = g_line[index - 1];
    }
    g_line[g_cursor] = character;
    ++g_line_length;
    ++g_cursor;

    // Printing the suffix redraws text that was shifted right by the insertion.
    // Move back over that suffix so the logical cursor remains after the new byte.
    for(uint32_t index = g_cursor - 1; index < g_line_length; ++index) {
        serial::putc(g_line[index]);
    }
    move_cursor_left(g_line_length - g_cursor);
}

void erase_before_cursor() {
    if(g_cursor == 0) {
        return;
    }

    --g_cursor;
    for(uint32_t index = g_cursor; index + 1 < g_line_length; ++index) {
        g_line[index] = g_line[index + 1];
    }
    --g_line_length;
    serial::putc('\b');
    redraw_suffix();
}

void erase_at_cursor() {
    if(g_cursor == g_line_length) {
        return;
    }

    for(uint32_t index = g_cursor; index + 1 < g_line_length; ++index) {
        g_line[index] = g_line[index + 1];
    }
    --g_line_length;
    redraw_suffix();
}

void consume_event(const keyboard::Event& event) {
    if(!event.pressed) {
        return;
    }

    if(event.key == keyboard::Key::Backspace) {
        erase_before_cursor();
        return;
    }

    if(event.key == keyboard::Key::Delete) {
        erase_at_cursor();
        return;
    }

    if(event.key == keyboard::Key::ArrowLeft) {
        if(g_cursor != 0) {
            --g_cursor;
            move_cursor_left(1);
        }
        return;
    }

    if(event.key == keyboard::Key::ArrowRight) {
        if(g_cursor < g_line_length) {
            ++g_cursor;
            move_cursor_right(1);
        }
        return;
    }

    if(event.key == keyboard::Key::Home) {
        move_cursor_left(g_cursor);
        g_cursor = 0;
        return;
    }

    if(event.key == keyboard::Key::End) {
        move_cursor_right(g_line_length - g_cursor);
        g_cursor = g_line_length;
        return;
    }

    if(event.key == keyboard::Key::Enter) {
        move_cursor_right(g_line_length - g_cursor);
        g_cursor = g_line_length;
        if(g_line_length < kLineCapacity) {
            g_line[g_line_length++] = '\n';
        }
        g_cursor = g_line_length;
        serial::putc('\n');
        return;
    }

    if(event.key == keyboard::Key::Character || event.key == keyboard::Key::Tab) {
        insert_character(event.character);
    }
}

} // namespace

namespace terminal {

bool initialize() {
    if(g_available) {
        return true;
    }
    g_line_length = 0;
    g_cursor = 0;
    g_available = true;
    return true;
}

bool is_available() {
    return g_available;
}

int64_t read(char* buffer, uint64_t length) {
    if(!g_available || buffer == nullptr || length == 0) {
        return 0;
    }

    for(;;) {
        keyboard::Event event = {};
        while(keyboard::poll(&event)) {
            consume_event(event);
            if(event.pressed && event.key == keyboard::Key::Enter) {
                const uint64_t copied = g_line_length < length ? g_line_length : length;
                for(uint64_t index = 0; index < copied; ++index) {
                    buffer[index] = g_line[index];
                }
                g_line_length = 0;
                g_cursor = 0;
                return static_cast<int64_t>(copied);
            }
        }

        // keyboard::read owns the atomic check-and-block operation. Waiting on that
        // queue avoids a second wakeup path and prevents a key from arriving between
        // the terminal's empty check and its scheduler block.
        if(!keyboard::read(&event)) {
            return -1;
        }
        consume_event(event);
        if(event.pressed && event.key == keyboard::Key::Enter) {
            const uint64_t copied = g_line_length < length ? g_line_length : length;
            for(uint64_t index = 0; index < copied; ++index) {
                buffer[index] = g_line[index];
            }
            g_line_length = 0;
            g_cursor = 0;
            return static_cast<int64_t>(copied);
        }
    }
}

int64_t write(const char* buffer, uint64_t length) {
    if(!g_available || buffer == nullptr) {
        return -1;
    }
    for(uint64_t index = 0; index < length; ++index) {
        serial::putc(buffer[index]);
    }
    return static_cast<int64_t>(length);
}

} // namespace terminal

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,
//           cppcoreguidelines-pro-bounds-constant-array-index,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic)
