#include "process.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "thread.hpp"
#include "thread_internal.hpp"
#include "vfs.hpp"
#include "virtual_memory.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace process {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) IDs survive process destruction
ProcessId g_next_process_id = 1;
Process* g_process_head = nullptr;
Process* g_process_tail = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

Process* create() {
    return create(nullptr);
}

Process* create(Process* parent) {
    if(g_next_process_id == 0) {
        return nullptr;
    }

    auto* process = static_cast<Process*>(kernel_heap::allocate(sizeof(Process)));
    if(process == nullptr) {
        return nullptr;
    }

    if(!virtual_memory::create_address_space(&process->address_space)) {
        kernel_heap::free(process);
        return nullptr;
    }

    process->id = g_next_process_id++;
    process->state = State::New;
    process->credentials = {.uid = 0, .gid = 0};
    process->thread_head = nullptr;
    process->thread_tail = nullptr;
    process->thread_count = 0;
    process->parent = nullptr;
    process->child_head = nullptr;
    process->child_tail = nullptr;
    process->sibling_next = nullptr;
    synchronization::initialize(&process->child_waiters);
    process->exit_status = 0;
    constexpr char kUnnamedImagePath[] = "[unnamed]";
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for(uint64_t index = 0; index < sizeof(kUnnamedImagePath); ++index) {
        process->image_path[index] = kUnnamedImagePath[index];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    process->all_next = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay, hicpp-no-array-decay)
    for(auto& descriptor : process->descriptors) {
        descriptor = {};
    }
    process->working_directory[0] = '/';
    process->working_directory[1] = '\0';
    process->descriptors[kStandardInput] = {
        .kind = DescriptorKind::StandardInput, .file = {}, .pipe = nullptr, .open = true
    };
    process->descriptors[kStandardOutput] = {
        .kind = DescriptorKind::StandardOutput, .file = {}, .pipe = nullptr, .open = true
    };
    process->descriptors[kStandardError] = {
        .kind = DescriptorKind::StandardError, .file = {}, .pipe = nullptr, .open = true
    };
    if(parent != nullptr && !set_parent(process, parent)) {
        (void)destroy(process);
        return nullptr;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    if(g_process_tail == nullptr) {
        g_process_head = process;
        g_process_tail = process;
    } else {
        g_process_tail->all_next = process;
        g_process_tail = process;
    }
    interrupts::restore(previous_state);
    return process;
}

bool destroy(Process* process) {
    if(process == nullptr || process->thread_count != 0 || process->state == State::Running ||
       process->parent != nullptr || process->child_head != nullptr ||
       virtual_memory::is_active(&process->address_space)) {
        return false;
    }

    close_file_descriptors(process);
    virtual_memory::destroy_address_space(&process->address_space);
    if(process->address_space.root_physical != 0) {
        return false;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    Process* previous = nullptr;
    for(auto* candidate = g_process_head; candidate != nullptr; candidate = candidate->all_next) {
        if(candidate != process) {
            previous = candidate;
            continue;
        }
        if(previous == nullptr) {
            g_process_head = candidate->all_next;
        } else {
            previous->all_next = candidate->all_next;
        }
        if(g_process_tail == candidate) {
            g_process_tail = previous;
        }
        break;
    }
    interrupts::restore(previous_state);
    return kernel_heap::free(process);
}

bool attach_thread(Process* process, kernel_thread::Thread* thread) {
    if(process == nullptr || thread == nullptr || thread->owner_process != nullptr || thread->process_next != nullptr ||
       thread->address_space != &process->address_space || thread->state == kernel_thread::State::Terminated) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    // The process list is the ownership boundary for the address space: linking the
    // thread and incrementing the count are one protected operation so destruction can
    // never observe a partially attached thread.
    thread->owner_process = process;
    thread->process_next = nullptr;
    if(process->thread_tail == nullptr) {
        process->thread_head = thread;
        process->thread_tail = thread;
    } else {
        process->thread_tail->process_next = thread;
        process->thread_tail = thread;
    }
    ++process->thread_count;
    process->state = State::Running;
    interrupts::restore(previous_state);
    return true;
}

bool detach_thread(kernel_thread::Thread* thread) {
    if(thread == nullptr || thread->owner_process == nullptr) {
        return true;
    }

    auto* process = thread->owner_process;
    const interrupts::State previous_state = interrupts::save_and_disable();
    kernel_thread::Thread* previous = nullptr;
    for(auto* candidate = process->thread_head; candidate != nullptr; candidate = candidate->process_next) {
        if(candidate != thread) {
            previous = candidate;
            continue;
        }

        if(previous == nullptr) {
            process->thread_head = candidate->process_next;
        } else {
            previous->process_next = candidate->process_next;
        }
        if(process->thread_tail == candidate) {
            process->thread_tail = previous;
        }
        --process->thread_count;
        // Detach before freeing a thread's stack so a terminated thread cannot keep a
        // process alive or retain a pointer to an address space that may be destroyed.
        if(process->thread_count == 0) {
            process->state = State::Terminated;
        }
        thread->process_next = nullptr;
        thread->owner_process = nullptr;
        thread->address_space = nullptr;
        interrupts::restore(previous_state);
        return true;
    }

    interrupts::restore(previous_state);
    return false;
}

ProcessId id(const Process* process) {
    return process == nullptr ? 0 : process->id;
}

Process* find(ProcessId process_id) {
    if(process_id == 0) {
        return nullptr;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    for(auto* candidate = g_process_head; candidate != nullptr; candidate = candidate->all_next) {
        if(candidate->id == process_id) {
            interrupts::restore(previous_state);
            return candidate;
        }
    }
    interrupts::restore(previous_state);
    return nullptr;
}

State state(const Process* process) {
    return process == nullptr ? State::Terminated : process->state;
}

virtual_memory::AddressSpace* address_space(Process* process) {
    return process == nullptr ? nullptr : &process->address_space;
}

Credentials credentials(const Process* process) {
    return process == nullptr ? Credentials{.uid = 0, .gid = 0} : process->credentials;
}

uint32_t thread_count(const Process* process) {
    return process == nullptr ? 0 : process->thread_count;
}

int64_t exit_status(const Process* process) {
    return process == nullptr ? 0 : process->exit_status;
}

bool info(uint64_t index, Info* output) {
    if(output == nullptr) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    auto* process = g_process_head;
    while(process != nullptr && index != 0) {
        process = process->all_next;
        --index;
    }
    if(process == nullptr) {
        interrupts::restore(previous_state);
        return false;
    }
    output->id = process->id;
    output->state = process->state;
    output->thread_count = process->thread_count;
    output->user_page_count = process->address_space.user_page_count;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for(uint32_t character = 0; character <= kMaximumImagePathLength; ++character) {
        output->image_path[character] = process->image_path[character];
        if(process->image_path[character] == '\0') {
            break;
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    interrupts::restore(previous_state);
    return true;
}

void set_image_path(Process* process, const char* path) {
    if(process == nullptr || path == nullptr) {
        return;
    }
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
    uint32_t index = 0;
    while(path[index] != '\0' && index < kMaximumImagePathLength) {
        process->image_path[index] = path[index];
        ++index;
    }
    process->image_path[index] = '\0';
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

bool inherit_descriptors(Process* child, const Process* parent) {
    if(child == nullptr || parent == nullptr) {
        return false;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    for(uint32_t descriptor = 0; descriptor < kMaximumFileDescriptors; ++descriptor) {
        child->descriptors[descriptor] = parent->descriptors[descriptor];
        if(parent->descriptors[descriptor].pipe != nullptr) {
            if(parent->descriptors[descriptor].kind == DescriptorKind::PipeRead) {
                ++child->descriptors[descriptor].pipe->readers;
            } else if(parent->descriptors[descriptor].kind == DescriptorKind::PipeWrite) {
                ++child->descriptors[descriptor].pipe->writers;
            }
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return true;
}

bool set_parent(Process* child, Process* parent) {
    if(child == nullptr || parent == nullptr || child == parent || child->parent != nullptr ||
       child->state == State::Terminated) {
        return false;
    }

    const interrupts::State previous_state = interrupts::save_and_disable();
    child->parent = parent;
    child->sibling_next = nullptr;
    if(parent->child_tail == nullptr) {
        parent->child_head = child;
        parent->child_tail = child;
    } else {
        parent->child_tail->sibling_next = child;
        parent->child_tail = child;
    }
    interrupts::restore(previous_state);
    return true;
}

void record_exit(Process* process, int64_t status) {
    if(process == nullptr || process->thread_count != 0) {
        return;
    }

    process->exit_status = status;
    process->state = State::Terminated;
    // A terminating process cannot remain the parent of live children: doing so
    // would make both the parent and its children permanently un-reapable. Orphaned
    // children become detached and will be reclaimed independently when they exit.
    while(process->child_head != nullptr) {
        auto* child = process->child_head;
        process->child_head = child->sibling_next;
        child->parent = nullptr;
        child->sibling_next = nullptr;
    }
    process->child_tail = nullptr;
    if(process->parent != nullptr) {
        // The terminating thread already holds interrupts disabled. Waking the parent
        // here makes the exit notification atomic with the transition to Terminated.
        (void)scheduler::wake_all(&process->parent->child_waiters);
    }
}

Process* find_child_locked(Process* parent, ProcessId child_id) {
    if(parent == nullptr) {
        return nullptr;
    }
    for(auto* child = parent->child_head; child != nullptr; child = child->sibling_next) {
        if(child->id == child_id) {
            return child;
        }
    }
    return nullptr;
}

bool reap_child_locked(Process* parent, Process* child, int64_t* status) {
    if(parent == nullptr || child == nullptr || child->parent != parent || child->state != State::Terminated) {
        return false;
    }

    Process* previous = nullptr;
    for(auto* candidate = parent->child_head; candidate != nullptr; candidate = candidate->sibling_next) {
        if(candidate != child) {
            previous = candidate;
            continue;
        }
        if(previous == nullptr) {
            parent->child_head = candidate->sibling_next;
        } else {
            previous->sibling_next = candidate->sibling_next;
        }
        if(parent->child_tail == candidate) {
            parent->child_tail = previous;
        }
        if(status != nullptr) {
            *status = child->exit_status;
        }
        child->parent = nullptr;
        child->sibling_next = nullptr;
        return true;
    }
    return false;
}

bool has_parent(const Process* process) {
    return process != nullptr && process->parent != nullptr;
}

int32_t allocate_file_descriptor(Process* process, const vfs::File* file) {
    if(process == nullptr || file == nullptr || !file->open) {
        return -1;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    for(uint32_t descriptor = kFirstFileDescriptor; descriptor < kMaximumFileDescriptors; ++descriptor) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
        if(!process->descriptors[descriptor].open) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
            process->descriptors[descriptor] = {
                .kind = DescriptorKind::File, .file = *file, .pipe = nullptr, .open = true
            };
            interrupts::restore(previous_state);
            return static_cast<int32_t>(descriptor);
        }
    }
    interrupts::restore(previous_state);
    return -1;
}

bool create_pipe(Process* process, int64_t descriptors[2]) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic,
    //              cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    if(process == nullptr || descriptors == nullptr) {
        return false;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    auto* pipe = static_cast<Pipe*>(kernel_heap::allocate(sizeof(Pipe)));
    if(pipe == nullptr) {
        interrupts::restore(previous_state);
        return false;
    }
    pipe->read_position = 0;
    pipe->write_position = 0;
    pipe->bytes = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    synchronization::initialize(&pipe->read_waiters);
    synchronization::initialize(&pipe->write_waiters);
    int32_t read_descriptor = -1;
    int32_t write_descriptor = -1;
    for(uint32_t descriptor = kFirstFileDescriptor; descriptor < kMaximumFileDescriptors; ++descriptor) {
        if(!process->descriptors[descriptor].open) {
            if(read_descriptor < 0) {
                read_descriptor = static_cast<int32_t>(descriptor);
            } else {
                write_descriptor = static_cast<int32_t>(descriptor);
                break;
            }
        }
    }
    if(read_descriptor < 0 || write_descriptor < 0) {
        (void)kernel_heap::free(pipe);
        interrupts::restore(previous_state);
        return false;
    }
    process->descriptors[read_descriptor] = {.kind = DescriptorKind::PipeRead, .file = {}, .pipe = pipe, .open = true};
    process->descriptors[write_descriptor] = {
        .kind = DescriptorKind::PipeWrite, .file = {}, .pipe = pipe, .open = true
    };
    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-pointer-arithmetic,
    //            cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    interrupts::restore(previous_state);
    return true;
}

int32_t duplicate_descriptor(Process* process, uint64_t descriptor, uint64_t target) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    if(process == nullptr || descriptor >= kMaximumFileDescriptors || target >= kMaximumFileDescriptors ||
       !process->descriptors[descriptor].open) {
        return -1;
    }
    if(descriptor == target) {
        return static_cast<int32_t>(target);
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    if(process->descriptors[target].open && !close_file_descriptor(process, target)) {
        interrupts::restore(previous_state);
        return -1;
    }
    process->descriptors[target] = process->descriptors[descriptor];
    if(process->descriptors[target].pipe != nullptr) {
        if(process->descriptors[target].kind == DescriptorKind::PipeRead) {
            ++process->descriptors[target].pipe->readers;
        } else if(process->descriptors[target].kind == DescriptorKind::PipeWrite) {
            ++process->descriptors[target].pipe->writers;
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    interrupts::restore(previous_state);
    return static_cast<int32_t>(target);
}

Pipe* pipe_descriptor(Process* process, uint64_t descriptor, DescriptorKind* kind) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    if(process == nullptr || descriptor >= kMaximumFileDescriptors || !process->descriptors[descriptor].open ||
       process->descriptors[descriptor].pipe == nullptr) {
        return nullptr;
    }
    if(kind != nullptr) {
        *kind = process->descriptors[descriptor].kind;
    }
    auto* pipe = process->descriptors[descriptor].pipe;
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return pipe;
}

vfs::File* file_descriptor(Process* process, uint64_t descriptor) {
    if(process == nullptr || descriptor >= kMaximumFileDescriptors ||
       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
       !process->descriptors[descriptor].open ||
       // Standard streams are not filesystem files; terminal handling will consume these kinds later.
       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
       process->descriptors[descriptor].kind != DescriptorKind::File) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
    return &process->descriptors[descriptor].file;
}

DescriptorKind descriptor_kind(const Process* process, uint64_t descriptor) {
    if(process == nullptr || descriptor >= kMaximumFileDescriptors ||
       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
       !process->descriptors[descriptor].open) {
        return DescriptorKind::Invalid;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
    return process->descriptors[descriptor].kind;
}

bool close_file_descriptor(Process* process, uint64_t descriptor) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
    if(process == nullptr || descriptor >= kMaximumFileDescriptors ||
       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
       !process->descriptors[descriptor].open) {
        return false;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
    if(process->descriptors[descriptor].kind == DescriptorKind::File &&
       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
       vfs::close(&process->descriptors[descriptor].file) != vfs::Status::Success) {
        interrupts::restore(previous_state);
        return false;
    }
    auto* pipe = process->descriptors[descriptor].pipe;
    if(pipe != nullptr) {
        if(process->descriptors[descriptor].kind == DescriptorKind::PipeRead) {
            --pipe->readers;
        } else if(process->descriptors[descriptor].kind == DescriptorKind::PipeWrite) {
            --pipe->writers;
        }
        // Endpoint closure changes EOF and full-buffer behavior, so wake both
        // sides even if no data was added.
        (void)scheduler::wake_all(&pipe->read_waiters);
        (void)scheduler::wake_all(&pipe->write_waiters);
        if(pipe->readers == 0 && pipe->writers == 0) {
            (void)kernel_heap::free(pipe);
        }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor was validated by helper
    process->descriptors[descriptor] = {};
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    interrupts::restore(previous_state);
    return true;
}

void close_file_descriptors(Process* process) {
    if(process == nullptr) {
        return;
    }
    for(uint32_t descriptor = 0; descriptor < kMaximumFileDescriptors; ++descriptor) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) descriptor is range-checked above
        if(process->descriptors[descriptor].open) {
            (void)close_file_descriptor(process, descriptor);
        }
    }
}

} // namespace process
