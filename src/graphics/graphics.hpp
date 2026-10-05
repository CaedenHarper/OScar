#pragma once

#include <stdint.h>

namespace graphics {

struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

struct Point {
    uint64_t x;
    uint64_t y;
};

struct Rectangle {
    uint64_t x;
    uint64_t y;
    uint64_t width;
    uint64_t height;
};

struct Surface {
    volatile uint8_t* address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bytes_per_pixel;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
    uint8_t red_size;
    uint8_t green_size;
    uint8_t blue_size;
};

/** Initialize the graphics renderer with a validated framebuffer surface. */
bool initialize(const Surface* surface);

/** Return whether the graphics renderer has an active drawing surface. */
bool is_available();

/** Return the active surface width, or zero before initialization. */
uint64_t width();

/** Return the active surface height, or zero before initialization. */
uint64_t height();

/** Fill the complete active surface with one color. */
void clear(Color color);

/** Draw a clipped filled rectangle on the active surface. */
void fill_rect(Rectangle rectangle, Color color);

/** Draw a clipped 5x7 bitmap-font text string at the given position. */
void draw_text(Point position, const char* text, Color foreground, Color background);

/** Draw the graphics milestone color bars, border, and diagnostic text. */
void draw_test_pattern();

} // namespace graphics
