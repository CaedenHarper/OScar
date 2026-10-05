#include "framebuffer.hpp"

#include "graphics.hpp"

#include <limine.h>
#include <limits.h>
#include <stdint.h>

namespace {

// This state describes one permanently available memory-mapped hardware surface.
graphics::Surface g_framebuffer = {}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr uint16_t kRgb24Bits = 24;
constexpr uint16_t kRgb32Bits = 32;

} // namespace

namespace framebuffer {

bool initialize(const limine_framebuffer* information) {
    if(information == nullptr || information->address == nullptr || information->width == 0 ||
       information->height == 0 || information->pitch == 0 || information->memory_model != LIMINE_FRAMEBUFFER_RGB ||
       (information->bpp != kRgb24Bits && information->bpp != kRgb32Bits)) {
        return false;
    }

    const uint64_t bytes_per_pixel = information->bpp / CHAR_BIT;
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
    return true;
}

bool is_available() {
    return g_framebuffer.address != nullptr;
}

bool surface(graphics::Surface* output) { // NOLINT(readability-non-const-parameter) output receives the surface.
    if(output == nullptr || !is_available()) {
        return false;
    }
    *output = g_framebuffer;
    return true;
}

uint64_t width() {
    return g_framebuffer.width;
}

uint64_t height() {
    return g_framebuffer.height;
}

} // namespace framebuffer
