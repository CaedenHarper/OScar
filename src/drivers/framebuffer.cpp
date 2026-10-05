#include "framebuffer.hpp"

#include <limine.h>
#include <stdint.h>

namespace {

struct Color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

volatile uint8_t* g_address = nullptr;
uint64_t g_width = 0;
uint64_t g_height = 0;
uint64_t g_pitch = 0;
uint16_t g_bytes_per_pixel = 0;
uint8_t g_red_shift = 0;
uint8_t g_green_shift = 0;
uint8_t g_blue_shift = 0;
uint8_t g_red_size = 0;
uint8_t g_green_size = 0;
uint8_t g_blue_size = 0;

uint32_t scale_channel(uint8_t channel, uint8_t bit_count) {
    if(bit_count == 0) {
        return 0;
    }
    if(bit_count >= 8) {
        return channel;
    }
    const uint32_t maximum = (1U << bit_count) - 1U;
    return (static_cast<uint32_t>(channel) * maximum + 127U) / 255U;
}

uint32_t pixel_value(Color color) {
    return (scale_channel(color.red, g_red_size) << g_red_shift) |
           (scale_channel(color.green, g_green_size) << g_green_shift) |
           (scale_channel(color.blue, g_blue_size) << g_blue_shift);
}

void put_pixel(uint64_t x, uint64_t y, Color color) {
    if(g_address == nullptr || x >= g_width || y >= g_height) {
        return;
    }

    const uint32_t value = pixel_value(color);
    volatile uint8_t* destination = g_address + (y * g_pitch) + (x * g_bytes_per_pixel);
    for(uint16_t byte = 0; byte < g_bytes_per_pixel; ++byte) {
        destination[byte] = static_cast<uint8_t>((value >> (byte * 8U)) & 0xffU);
    }
}

void draw_test_pattern() {
    constexpr Color kColors[] = {
        {255, 0, 0},
        {255, 128, 0},
        {255, 255, 0},
        {0, 192, 0},
        {0, 128, 255},
        {0, 0, 192},
        {128, 0, 255},
        {255, 0, 128},
    };
    constexpr uint64_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

    for(uint64_t y = 0; y < g_height; ++y) {
        const uint64_t bar = (y * kColorCount) / g_height;
        for(uint64_t x = 0; x < g_width; ++x) {
            put_pixel(x, y, kColors[bar]);
        }
    }

    // A black and white border makes pitch, clipping, and orientation errors
    // obvious even before text rendering is available.
    const uint64_t border = g_width < g_height ? g_width / 32 : g_height / 32;
    for(uint64_t y = 0; y < g_height; ++y) {
        for(uint64_t x = 0; x < g_width; ++x) {
            if(x < border || y < border || x >= g_width - border || y >= g_height - border) {
                put_pixel(x, y, ((x / 16 + y / 16) % 2 == 0) ? Color{255, 255, 255} : Color{0, 0, 0});
            }
        }
    }
}

} // namespace

namespace framebuffer {

bool initialize(const limine_framebuffer* information) {
    if(information == nullptr || information->address == nullptr || information->width == 0 ||
       information->height == 0 || information->pitch == 0 || information->memory_model != LIMINE_FRAMEBUFFER_RGB ||
       (information->bpp != 24 && information->bpp != 32)) {
        return false;
    }

    const uint64_t bytes_per_pixel = information->bpp / 8;
    if(information->width > UINT64_MAX / bytes_per_pixel || information->pitch < information->width * bytes_per_pixel) {
        return false;
    }

    g_address = static_cast<volatile uint8_t*>(information->address);
    g_width = information->width;
    g_height = information->height;
    g_pitch = information->pitch;
    g_bytes_per_pixel = static_cast<uint16_t>(bytes_per_pixel);
    g_red_shift = information->red_mask_shift;
    g_green_shift = information->green_mask_shift;
    g_blue_shift = information->blue_mask_shift;
    g_red_size = information->red_mask_size;
    g_green_size = information->green_mask_size;
    g_blue_size = information->blue_mask_size;

    draw_test_pattern();
    return true;
}

bool is_available() {
    return g_address != nullptr;
}

uint64_t width() {
    return g_width;
}

uint64_t height() {
    return g_height;
}

} // namespace framebuffer
