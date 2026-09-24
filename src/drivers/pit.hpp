#pragma once

#include <stdint.h>

namespace pit {

/*
 * Program PIT channel zero in periodic mode. Returns the actual configured
 * frequency through the output argument, or false for an unsupported request.
 */
bool initialize(uint32_t requested_frequency_hz, uint32_t* configured_frequency_hz);

} // namespace pit
