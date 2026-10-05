#include "graphics.hpp"

#include <stdint.h>

namespace {

graphics::Surface g_surface = {}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr uint32_t kChannelBits = 8;
constexpr uint32_t kChannelMaximum = UINT8_MAX;
constexpr uint32_t kChannelRounding = 127;
constexpr uint32_t kByteMask = UINT8_MAX;
constexpr uint64_t kColorBarCount = 8;
constexpr uint64_t kCheckerSize = 16;
constexpr uint64_t kBorderDivisor = 32;
constexpr uint64_t kGlyphWidth = 5;
constexpr uint64_t kGlyphHeight = 7;
constexpr uint64_t kGlyphScale = 2;
constexpr uint64_t kGlyphSpacing = 2;
constexpr uint64_t kUppercaseGlyphStart = 10;
constexpr uint64_t kSpaceGlyphIndex = 36;
constexpr uint8_t kHalfChannel = 128;
constexpr uint8_t kThreeQuarterChannel = 192;

constexpr graphics::Color kBlack = {.red = 0, .green = 0, .blue = 0, .alpha = UINT8_MAX};
constexpr graphics::Color kWhite = {.red = UINT8_MAX, .green = UINT8_MAX, .blue = UINT8_MAX, .alpha = UINT8_MAX};
constexpr graphics::Color kColors[kColorBarCount] = {
    {.red = UINT8_MAX, .green = 0, .blue = 0, .alpha = UINT8_MAX},
    {.red = UINT8_MAX, .green = kHalfChannel, .blue = 0, .alpha = UINT8_MAX},
    {.red = UINT8_MAX, .green = UINT8_MAX, .blue = 0, .alpha = UINT8_MAX},
    {.red = 0, .green = kThreeQuarterChannel, .blue = 0, .alpha = UINT8_MAX},
    {.red = 0, .green = kHalfChannel, .blue = UINT8_MAX, .alpha = UINT8_MAX},
    {.red = 0, .green = 0, .blue = kThreeQuarterChannel, .alpha = UINT8_MAX},
    {.red = kHalfChannel, .green = 0, .blue = UINT8_MAX, .alpha = UINT8_MAX},
    {.red = UINT8_MAX, .green = 0, .blue = kHalfChannel, .alpha = UINT8_MAX},
};

// Each row is a 5-bit bitmap. This is fixed bitmap data rather than numeric logic.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
constexpr uint8_t kFont[][kGlyphHeight] = {
    {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
    {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
    {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
    {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
    {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
    {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11},
    {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
    {0x11, 0x0a, 0x04, 0x04, 0x04, 0x0a, 0x11},
    {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
    {0, 0, 0, 0, 0, 0, 0},
};
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) both values are channel-scale inputs.
uint32_t scale_channel(uint8_t channel, uint8_t bit_count) {
    if(bit_count == 0) {
        return 0;
    }
    if(bit_count >= kChannelBits) {
        return channel;
    }
    const uint32_t maximum = (1U << bit_count) - 1U;
    return ((static_cast<uint32_t>(channel) * maximum) + kChannelRounding) / kChannelMaximum;
}

uint32_t pixel_value(graphics::Color color) {
    return (scale_channel(color.red, g_surface.red_size) << g_surface.red_shift) |
           (scale_channel(color.green, g_surface.green_size) << g_surface.green_shift) |
           (scale_channel(color.blue, g_surface.blue_size) << g_surface.blue_shift);
}

void put_pixel(uint64_t pixel_x, uint64_t pixel_y, graphics::Color color) {
    if(g_surface.address == nullptr || pixel_x >= g_surface.width || pixel_y >= g_surface.height) {
        return;
    }

    const uint32_t value = pixel_value(color);
    // The framebuffer is a raw device surface, so its byte addressing cannot be
    // expressed as a bounded C++ array. The bounds were checked above.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    volatile uint8_t* destination =
        g_surface.address + (pixel_y * g_surface.pitch) + (pixel_x * g_surface.bytes_per_pixel);
    for(uint16_t byte = 0; byte < g_surface.bytes_per_pixel; ++byte) {
        destination[byte] = static_cast<uint8_t>((value >> (byte * kChannelBits)) & kByteMask);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

uint64_t glyph_index(char character) {
    if(character >= '0' && character <= '9') {
        return static_cast<uint64_t>(character - '0');
    }
    if(character >= 'A' && character <= 'Z') {
        return kUppercaseGlyphStart + static_cast<uint64_t>(character - 'A');
    }
    if(character == ' ') {
        return kSpaceGlyphIndex;
    }
    return UINT64_MAX;
}

char uppercase(char character) {
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
}

void draw_glyph(graphics::Point position, char character, graphics::Color foreground, graphics::Color background) {
    const uint64_t index = glyph_index(uppercase(character));
    if(index == UINT64_MAX) {
        return;
    }
    for(uint64_t row = 0; row < kGlyphHeight; ++row) {
        for(uint64_t column = 0; column < kGlyphWidth; ++column) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) both indices are bounded above.
            const bool set = (kFont[index][row] & (1U << (kGlyphWidth - column - 1))) != 0;
            const graphics::Color color = set ? foreground : background;
            fill_rect(
                {.x = position.x + (column * kGlyphScale),
                 .y = position.y + (row * kGlyphScale),
                 .width = kGlyphScale,
                 .height = kGlyphScale},
                color
            );
        }
    }
}

} // namespace

namespace graphics {

bool initialize(const Surface* surface) {
    if(surface == nullptr || surface->address == nullptr || surface->width == 0 || surface->height == 0) {
        return false;
    }
    g_surface = *surface;
    return true;
}

bool is_available() {
    return g_surface.address != nullptr;
}

uint64_t width() {
    return g_surface.width;
}

uint64_t height() {
    return g_surface.height;
}

void clear(Color color) {
    fill_rect({.x = 0, .y = 0, .width = g_surface.width, .height = g_surface.height}, color);
}

void fill_rect(Rectangle rectangle, Color color) {
    if(!is_available() || rectangle.x >= g_surface.width || rectangle.y >= g_surface.height) {
        return;
    }
    const uint64_t end_x =
        rectangle.width > g_surface.width - rectangle.x ? g_surface.width : rectangle.x + rectangle.width;
    const uint64_t end_y =
        rectangle.height > g_surface.height - rectangle.y ? g_surface.height : rectangle.y + rectangle.height;
    for(uint64_t row = rectangle.y; row < end_y; ++row) {
        for(uint64_t column = rectangle.x; column < end_x; ++column) {
            put_pixel(column, row, color);
        }
    }
}

void draw_text(Point position, const char* text, Color foreground, Color background) {
    if(text == nullptr) {
        return;
    }
    uint64_t cursor_x = position.x;
    uint64_t cursor_y = position.y;
    while(*text != '\0') {
        if(*text == '\n') {
            cursor_x = position.x;
            cursor_y += (kGlyphHeight + kGlyphSpacing) * kGlyphScale;
        } else {
            draw_glyph({.x = cursor_x, .y = cursor_y}, *text, foreground, background);
            cursor_x += (kGlyphWidth + kGlyphSpacing) * kGlyphScale;
        }
        ++text; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) text is a null-terminated string.
    }
}

void draw_test_pattern() {
    if(!is_available()) {
        return;
    }
    for(uint64_t index = 0; index < kColorBarCount; ++index) {
        const uint64_t top = (index * g_surface.height) / kColorBarCount;
        const uint64_t bottom = ((index + 1) * g_surface.height) / kColorBarCount;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) index is bounded by the loop.
        fill_rect({.x = 0, .y = top, .width = g_surface.width, .height = bottom - top}, kColors[index]);
    }

    const uint64_t border = (g_surface.width < g_surface.height ? g_surface.width : g_surface.height) / kBorderDivisor;
    for(uint64_t row = 0; row < g_surface.height; ++row) {
        for(uint64_t column = 0; column < g_surface.width; ++column) {
            if(column < border || row < border || column >= g_surface.width - border ||
               row >= g_surface.height - border) {
                const Color color = (((column / kCheckerSize) + (row / kCheckerSize)) % 2 == 0) ? kWhite : kBlack;
                put_pixel(column, row, color);
            }
        }
    }

    draw_text({.x = border * 2, .y = border * 2}, "OSCAR GRAPHICS INITIALIZED\nFRAMEBUFFER READY", kWhite, kBlack);
}

} // namespace graphics
