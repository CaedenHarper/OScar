#include "syscalls.hpp"

#include "scheduler.hpp"
#include "serial.hpp"
#include "user_memory.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t kWrite = 0;
constexpr uint64_t kExit = 1;
constexpr uint64_t kYield = 2;
constexpr uint64_t kSleep = 3;
constexpr uint64_t kMaximumWriteLength = 4096;
constexpr int64_t kErrorInvalidArgument = -1;
constexpr int64_t kErrorUnknownCall = -2;

int64_t write(const syscalls::Frame* frame) {
    if(frame->rsi > kMaximumWriteLength) {
        return kErrorInvalidArgument;
    }

    constexpr uint64_t kBufferSize = 128;
    char buffer[kBufferSize];
    uint64_t copied = 0;
    while(copied < frame->rsi) {
        const uint64_t chunk = frame->rsi - copied > kBufferSize ? kBufferSize : frame->rsi - copied;
        if(!user_memory::copy_from_user(
               static_cast<void*>(buffer), frame->rdi + copied, chunk
           )) { // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            return kErrorInvalidArgument;
        }
        for(uint64_t index = 0; index < chunk; ++index) {
            serial::putc(buffer[index]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
        copied += chunk;
    }
    return static_cast<int64_t>(copied);
}

} // namespace

namespace syscalls {

extern "C" void handle(Frame* frame) {
    if(frame == nullptr || (frame->cs & 3U) != 3U) {
        return;
    }

    switch(frame->rax) {
        case kWrite:
            frame->rax = static_cast<uint64_t>(write(frame));
            return;
        case kExit:
            scheduler::thread_exit(scheduler::current());
        case kYield:
            scheduler::yield();
            frame->rax = 0;
            return;
        case kSleep:
            scheduler::sleep(frame->rdi);
            frame->rax = 0;
            return;
        default:
            frame->rax = static_cast<uint64_t>(kErrorUnknownCall);
            return;
    }
}

} // namespace syscalls
