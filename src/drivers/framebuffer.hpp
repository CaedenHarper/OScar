#pragma once

#include <stdint.h>

struct limine_framebuffer;

namespace framebuffer {

/**
 * Initialize the first Limine-provided RGB framebuffer and draw a test pattern.
 * The Limine framebuffer request and virtual memory must be initialized before
 * this function is called. The framebuffer remains owned by the bootloader and
 * is only written through the address supplied in its response.
 * Returns false when no compatible framebuffer is available.
 */
bool initialize(const limine_framebuffer* information);

/** Return whether the framebuffer initialized successfully. */
bool is_available();

/** Return the framebuffer width in pixels, or zero before initialization. */
uint64_t width();

/** Return the framebuffer height in pixels, or zero before initialization. */
uint64_t height();

} // namespace framebuffer
