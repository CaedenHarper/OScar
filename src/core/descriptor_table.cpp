#include "descriptor_table.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "network.hpp"
#include "process_internal.hpp"
#include "scheduler.hpp"
#include "vfs.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

// Descriptor slots and pipe endpoints are fixed-size kernel tables. Every dynamic index is
// range-checked by valid() or by the allocation loop before access.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-pro-bounds-pointer-arithmetic)
namespace descriptor {

namespace {

Entry standard_entry(Kind kind) {
    return {.kind = kind, .file = {}, .pipe = nullptr, .socket = nullptr, .open = true};
}

bool valid(const Table* table, uint64_t descriptor) {
    return table != nullptr && descriptor < kMaximumCount && table->entries[descriptor].open;
}

void acquire(const Entry* entry) {
    if(entry->pipe != nullptr) {
        if(entry->kind == Kind::PipeRead) {
            ++entry->pipe->readers;
        } else if(entry->kind == Kind::PipeWrite) {
            ++entry->pipe->writers;
        }
    }
    if(entry->socket != nullptr) {
        ++entry->socket->references;
    }
}

} // namespace

void initialize(Table* table) {
    if(table == nullptr) {
        return;
    }
    for(auto& entry : table->entries) {
        entry = {};
    }
    table->entries[kStandardInput] = standard_entry(Kind::StandardInput);
    table->entries[kStandardOutput] = standard_entry(Kind::StandardOutput);
    table->entries[kStandardError] = standard_entry(Kind::StandardError);
}

bool inherit(Table* child, const Table* parent) {
    if(child == nullptr || parent == nullptr || child == parent) {
        return false;
    }
    close_all(child);
    for(uint32_t descriptor = 0; descriptor < kMaximumCount; ++descriptor) {
        child->entries[descriptor] = parent->entries[descriptor];
        if(parent->entries[descriptor].open) {
            acquire(&parent->entries[descriptor]);
        }
    }
    return true;
}

int32_t allocate_file(Table* table, const vfs::File* file) {
    if(table == nullptr || file == nullptr || !file->open) {
        return -1;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    for(uint32_t descriptor = kFirstAllocatable; descriptor < kMaximumCount; ++descriptor) {
        if(!table->entries[descriptor].open) {
            table->entries[descriptor] = {
                .kind = Kind::File, .file = *file, .pipe = nullptr, .socket = nullptr, .open = true
            };
            interrupts::restore(previous_state);
            return static_cast<int32_t>(descriptor);
        }
    }
    interrupts::restore(previous_state);
    return -1;
}

bool create_pipe(Table* table, int64_t descriptors[2]) {
    if(table == nullptr || descriptors == nullptr) {
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
    for(uint32_t descriptor = kFirstAllocatable; descriptor < kMaximumCount; ++descriptor) {
        if(!table->entries[descriptor].open) {
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
    table->entries[read_descriptor] = {
        .kind = Kind::PipeRead, .file = {}, .pipe = pipe, .socket = nullptr, .open = true
    };
    table->entries[write_descriptor] = {
        .kind = Kind::PipeWrite, .file = {}, .pipe = pipe, .socket = nullptr, .open = true
    };
    descriptors[0] = read_descriptor;
    descriptors[1] = write_descriptor;
    interrupts::restore(previous_state);
    return true;
}

int32_t allocate_socket(Table* table, Socket* socket) {
    if(table == nullptr || socket == nullptr) {
        return -1;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    for(uint32_t descriptor = kFirstAllocatable; descriptor < kMaximumCount; ++descriptor) {
        if(!table->entries[descriptor].open) {
            table->entries[descriptor] = {
                .kind = Kind::Socket, .file = {}, .pipe = nullptr, .socket = socket, .open = true
            };
            interrupts::restore(previous_state);
            return static_cast<int32_t>(descriptor);
        }
    }
    interrupts::restore(previous_state);
    return -1;
}

int32_t duplicate(Table* table, uint64_t descriptor, uint64_t target) {
    if(!valid(table, descriptor) || target >= kMaximumCount) {
        return -1;
    }
    if(descriptor == target) {
        return static_cast<int32_t>(target);
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    if(valid(table, target) && !close(table, target)) {
        interrupts::restore(previous_state);
        return -1;
    }
    table->entries[target] = table->entries[descriptor];
    acquire(&table->entries[target]);
    interrupts::restore(previous_state);
    return static_cast<int32_t>(target);
}

Kind kind(const Table* table, uint64_t descriptor) {
    return valid(table, descriptor) ? table->entries[descriptor].kind : Kind::Invalid;
}

Pipe* pipe(Table* table, uint64_t descriptor, Kind* output_kind) {
    if(!valid(table, descriptor) || table->entries[descriptor].pipe == nullptr) {
        return nullptr;
    }
    if(output_kind != nullptr) {
        *output_kind = table->entries[descriptor].kind;
    }
    return table->entries[descriptor].pipe;
}

Socket* socket(Table* table, uint64_t descriptor) {
    if(!valid(table, descriptor) || table->entries[descriptor].kind != Kind::Socket) {
        return nullptr;
    }
    return table->entries[descriptor].socket;
}

vfs::File* file(Table* table, uint64_t descriptor) {
    if(!valid(table, descriptor) || table->entries[descriptor].kind != Kind::File) {
        return nullptr;
    }
    return &table->entries[descriptor].file;
}

bool close(Table* table, uint64_t descriptor) {
    return close(table, descriptor, 0);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) descriptor and timeout are distinct ABI values.
bool close(Table* table, uint64_t descriptor, uint64_t socket_timeout_ticks) {
    if(!valid(table, descriptor)) {
        return false;
    }
    const interrupts::State previous_state = interrupts::save_and_disable();
    auto& entry = table->entries[descriptor];
    if(entry.kind == Kind::File && vfs::close(&entry.file) != vfs::Status::Success) {
        interrupts::restore(previous_state);
        return false;
    }
    if(entry.pipe != nullptr) {
        if(entry.kind == Kind::PipeRead) {
            --entry.pipe->readers;
        } else if(entry.kind == Kind::PipeWrite) {
            --entry.pipe->writers;
        }
        (void)scheduler::wake_all(&entry.pipe->read_waiters);
        (void)scheduler::wake_all(&entry.pipe->write_waiters);
        if(entry.pipe->readers == 0 && entry.pipe->writers == 0) {
            (void)kernel_heap::free(entry.pipe);
        }
    }
    if(entry.socket != nullptr) {
        if(entry.socket->references == 1 && socket_timeout_ticks != 0) {
            (void)network::tcp_close(&entry.socket->connection, socket_timeout_ticks);
        }
        --entry.socket->references;
        if(entry.socket->references == 0) {
            network::tcp_unregister(&entry.socket->connection);
            (void)kernel_heap::free(entry.socket);
        }
    }
    entry = {};
    interrupts::restore(previous_state);
    return true;
}

void close_all(Table* table) {
    if(table == nullptr) {
        return;
    }
    for(uint32_t descriptor = 0; descriptor < kMaximumCount; ++descriptor) {
        (void)close(table, descriptor);
    }
}

} // namespace descriptor

// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index, cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-pro-bounds-pointer-arithmetic)

namespace process {

int32_t allocate_file_descriptor(Process* process, const vfs::File* file) {
    return process == nullptr ? -1 : descriptor::allocate_file(&process->descriptors, file);
}

bool create_pipe(Process* process, int64_t descriptors[2]) {
    return process != nullptr && descriptor::create_pipe(&process->descriptors, descriptors);
}

int32_t allocate_socket(Process* process, Socket* socket) {
    return process == nullptr ? -1 : descriptor::allocate_socket(&process->descriptors, socket);
}

int32_t duplicate_descriptor(Process* process, uint64_t descriptor, uint64_t target) {
    return process == nullptr ? -1 : descriptor::duplicate(&process->descriptors, descriptor, target);
}

Socket* socket_descriptor(Process* process, uint64_t descriptor) {
    return process == nullptr ? nullptr : descriptor::socket(&process->descriptors, descriptor);
}

Pipe* pipe_descriptor(Process* process, uint64_t descriptor, DescriptorKind* kind) {
    if(process == nullptr) {
        return nullptr;
    }
    descriptor::Kind output_kind = descriptor::Kind::Invalid;
    auto* result = descriptor::pipe(&process->descriptors, descriptor, kind == nullptr ? nullptr : &output_kind);
    if(kind != nullptr && result != nullptr) {
        *kind = static_cast<DescriptorKind>(output_kind);
    }
    return result;
}

vfs::File* file_descriptor(Process* process, uint64_t descriptor) {
    return process == nullptr ? nullptr : descriptor::file(&process->descriptors, descriptor);
}

DescriptorKind descriptor_kind(const Process* process, uint64_t descriptor) {
    return process == nullptr ? DescriptorKind::Invalid
                              : static_cast<DescriptorKind>(descriptor::kind(&process->descriptors, descriptor));
}

bool close_file_descriptor(Process* process, uint64_t descriptor) {
    return process != nullptr && descriptor::close(&process->descriptors, descriptor);
}

void close_file_descriptors(Process* process) {
    if(process != nullptr) {
        descriptor::close_all(&process->descriptors);
    }
}

} // namespace process
