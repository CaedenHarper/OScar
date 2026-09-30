#pragma once

#include <stdint.h>

namespace startup {

/**
 * Initialize the production kernel subsystems that are normally covered by the
 * boot smoke suite. Physical memory must already be initialized; returns false
 * if a required subsystem cannot be initialized.
 */
bool initialize(uintptr_t hhdm_offset);

/**
 * Load and enqueue the filesystem-backed `/sbin/init` process. Scheduler and
 * ELF-loader prerequisites must be initialized first; returns false without
 * starting scheduling when init cannot be loaded.
 */
bool prepare_init();

} // namespace startup
