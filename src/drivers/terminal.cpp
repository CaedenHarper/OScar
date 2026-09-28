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
bool g_available = false;

void echo_backspace() {
    serial::putc('\b');
    serial::putc(' ');
    serial::putc('\b');
}

void consume_event(const keyboard::Event& event) {
    if(!event.pressed) {
        return;
    }

    if(event.key == keyboard::Key::Backspace) {
        if(g_line_length != 0) {
            --g_line_length;
            echo_backspace();
        }
        return;
    }

    if(event.key == keyboard::Key::Enter) {
        if(g_line_length < kLineCapacity) {
            g_line[g_line_length++] = '\n';
        }
        serial::putc('\n');
        return;
    }

    if(event.key == keyboard::Key::Character || event.key == keyboard::Key::Tab) {
        if(g_line_length + 1 < kLineCapacity) {
            g_line[g_line_length++] = event.character;
            serial::putc(event.character);
        }
    }
}

} // namespace

namespace terminal {

bool initialize() {
    if(g_available) {
        return true;
    }
    g_line_length = 0;
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
