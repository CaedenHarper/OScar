#pragma once

#include "graphics.hpp"

#include <stdint.h>

struct limine_framebuffer;

namespace framebuffer {

/**
 * Initialize the first Limine-provided RGB framebuffer.
 * The Limine framebuffer request and virtual memory must be initialized before
 * this function is called. The framebuffer remains owned by the bootloader and
 * is only written through the address supplied in its response.
 * Returns false when no compatible framebuffer is available.
 */
bool initialize(const limine_framebuffer* information);

/** Copy the initialized framebuffer surface description into output. */
bool surface(graphics::Surface* output);

/** Return whether the framebuffer initialized successfully. */
bool is_available();

/** Return the framebuffer width in pixels, or zero before initialization. */
uint64_t width();

/** Return the framebuffer height in pixels, or zero before initialization. */
uint64_t height();

} // namespace framebuffer
