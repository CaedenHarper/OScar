#include "startup.hpp"

#include "kernel_heap.hpp"
#include "loader.hpp"
#include "network.hpp"
#include "process.hpp"
#include "scheduler.hpp"
#include "thread.hpp"
#include "vfs.hpp"
#include "virtio_block.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace startup {

bool initialize(uintptr_t hhdm_offset) {
    virtual_memory::initialize(hhdm_offset);
    kernel_heap::initialize();
    // Production init is loaded from disk, so the runtime profile must perform
    // the small subset of storage setup that the test profile normally covers.
    // Keeping this here prevents the production boot path from depending on a
    // test function merely to make `/sbin/init` available.
    if(!virtio_block::initialize() || virtio_block::device() == nullptr || !vfs::mount_root(virtio_block::device())) {
        return false;
    }
    // Networking is optional during boot; the ping syscall reports an explicit
    // unavailable status when no VirtIO network function is present.
    (void)network::initialize();
    return scheduler::initialize();
}

bool prepare_init() {
    // loader::load_path writes both output handles, so this pointer variable
    // cannot be const even though startup never mutates the resulting process.
    process::Process* process = nullptr; // NOLINT(misc-const-correctness)
    kernel_thread::Thread* thread = nullptr;
    if(!loader::load_path("/sbin/init", nullptr, &process, &thread) || process == nullptr || thread == nullptr) {
        return false;
    }

    return scheduler::enqueue(thread);
}

} // namespace startup
