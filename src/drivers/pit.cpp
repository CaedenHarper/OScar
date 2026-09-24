#include "pit.hpp"

#include "io.hpp"

#include <stdint.h>

namespace {

constexpr uint32_t kPitBaseFrequency = 1193182;
constexpr uint32_t kMinimumDivisor = 1;
constexpr uint32_t kMaximumDivisor = 0xffff;
constexpr uint16_t kChannelZeroDataPort = 0x40;
constexpr uint16_t kCommandPort = 0x43;
constexpr uint8_t kChannelZeroPeriodicCommand = 0x36;
constexpr uint32_t kByteMask = 0xffU;
constexpr unsigned kHighByteShift = 8U;

} // namespace

namespace pit {

bool initialize(uint32_t requested_frequency_hz, uint32_t* configured_frequency_hz) {
    if(configured_frequency_hz == nullptr || requested_frequency_hz == 0) {
        return false;
    }

    uint32_t divisor = (kPitBaseFrequency + (requested_frequency_hz / 2)) / requested_frequency_hz;
    if(divisor < kMinimumDivisor) {
        divisor = kMinimumDivisor;
    }
    if(divisor > kMaximumDivisor) {
        return false;
    }

    io::out8(kCommandPort, kChannelZeroPeriodicCommand);
    io::out8(kChannelZeroDataPort, static_cast<uint8_t>(divisor & kByteMask));
    io::out8(kChannelZeroDataPort, static_cast<uint8_t>((divisor >> kHighByteShift) & kByteMask));
    *configured_frequency_hz = kPitBaseFrequency / divisor;
    return true;
}

} // namespace pit
