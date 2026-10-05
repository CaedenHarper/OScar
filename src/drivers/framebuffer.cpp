#include "framebuffer.hpp"

#include <limine.h>
#include <stdint.h>

namespace {

struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct FramebufferState {
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

// This state describes one permanently available memory-mapped hardware surface.
FramebufferState g_framebuffer = {}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr uint32_t kChannelBits = 8;
constexpr uint32_t kChannelMaximum = UINT8_MAX;
constexpr uint32_t kChannelRounding = 127;
constexpr uint32_t kByteMask = UINT8_MAX;
constexpr uint64_t kCheckerSize = 16;
constexpr uint64_t kColorCount = 8;
constexpr uint16_t kRgb24Bits = 24;
constexpr uint16_t kRgb32Bits = 32;
constexpr uint8_t kHalfChannel = 128;
constexpr uint8_t kThreeQuarterChannel = 192;

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

uint32_t pixel_value(Color color) {
    return (scale_channel(color.red, g_framebuffer.red_size) << g_framebuffer.red_shift) |
           (scale_channel(color.green, g_framebuffer.green_size) << g_framebuffer.green_shift) |
           (scale_channel(color.blue, g_framebuffer.blue_size) << g_framebuffer.blue_shift);
}

void put_pixel(uint64_t pixel_x, uint64_t pixel_y, Color color) {
    if(g_framebuffer.address == nullptr || pixel_x >= g_framebuffer.width || pixel_y >= g_framebuffer.height) {
        return;
    }

    const uint32_t value = pixel_value(color);
    // The framebuffer is a raw device surface, so its byte addressing cannot be
    // expressed as a bounded C++ array. The bounds were checked above.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    volatile uint8_t* destination =
        g_framebuffer.address + (pixel_y * g_framebuffer.pitch) + (pixel_x * g_framebuffer.bytes_per_pixel);
    for(uint16_t byte = 0; byte < g_framebuffer.bytes_per_pixel; ++byte) {
        destination[byte] = static_cast<uint8_t>((value >> (byte * kChannelBits)) & kByteMask);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

void draw_test_pattern() {
    constexpr Color kColors[] = {
        {.red = UINT8_MAX, .green = 0, .blue = 0},
        {.red = UINT8_MAX, .green = kHalfChannel, .blue = 0},
        {.red = UINT8_MAX, .green = UINT8_MAX, .blue = 0},
        {.red = 0, .green = kThreeQuarterChannel, .blue = 0},
        {.red = 0, .green = kHalfChannel, .blue = UINT8_MAX},
        {.red = 0, .green = 0, .blue = kThreeQuarterChannel},
        {.red = kHalfChannel, .green = 0, .blue = UINT8_MAX},
        {.red = UINT8_MAX, .green = 0, .blue = kHalfChannel},
    };

    for(uint64_t row = 0; row < g_framebuffer.height; ++row) {
        const uint64_t bar = (row * kColorCount) / g_framebuffer.height;
        for(uint64_t column = 0; column < g_framebuffer.width; ++column) {
            // bar is bounded by the calculation above.
            put_pixel(column, row, kColors[bar]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
    }

    // A black and white border makes pitch, clipping, and orientation errors
    // obvious even before text rendering is available.
    const uint64_t border =
        g_framebuffer.width < g_framebuffer.height ? g_framebuffer.width / 32 : g_framebuffer.height / 32;
    for(uint64_t row = 0; row < g_framebuffer.height; ++row) {
        for(uint64_t column = 0; column < g_framebuffer.width; ++column) {
            if(column < border || row < border || column >= g_framebuffer.width - border ||
               row >= g_framebuffer.height - border) {
                const Color color = (((column / kCheckerSize) + (row / kCheckerSize)) % 2 == 0)
                                        ? Color{.red = UINT8_MAX, .green = UINT8_MAX, .blue = UINT8_MAX}
                                        : Color{.red = 0, .green = 0, .blue = 0};
                put_pixel(column, row, color);
            }
        }
    }
}

} // namespace

namespace framebuffer {

bool initialize(const limine_framebuffer* information) {
    if(information == nullptr || information->address == nullptr || information->width == 0 ||
       information->height == 0 || information->pitch == 0 || information->memory_model != LIMINE_FRAMEBUFFER_RGB ||
       (information->bpp != kRgb24Bits && information->bpp != kRgb32Bits)) {
        return false;
    }

    const uint64_t bytes_per_pixel = information->bpp / 8;
    if(information->width > UINT64_MAX / bytes_per_pixel || information->pitch < information->width * bytes_per_pixel) {
        return false;
    }

    g_framebuffer.address = static_cast<volatile uint8_t*>(information->address);
    g_framebuffer.width = information->width;
    g_framebuffer.height = information->height;
    g_framebuffer.pitch = information->pitch;
    g_framebuffer.bytes_per_pixel = static_cast<uint16_t>(bytes_per_pixel);
    g_framebuffer.red_shift = information->red_mask_shift;
    g_framebuffer.green_shift = information->green_mask_shift;
    g_framebuffer.blue_shift = information->blue_mask_shift;
    g_framebuffer.red_size = information->red_mask_size;
    g_framebuffer.green_size = information->green_mask_size;
    g_framebuffer.blue_size = information->blue_mask_size;

    draw_test_pattern();
    return true;
}

bool is_available() {
    return g_framebuffer.address != nullptr;
}

uint64_t width() {
    return g_framebuffer.width;
}

uint64_t height() {
    return g_framebuffer.height;
}

} // namespace framebuffer
