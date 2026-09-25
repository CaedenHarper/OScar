#include "process.hpp"

#include "interrupts.hpp"
#include "kernel_heap.hpp"
#include "process_internal.hpp"
#include "thread.hpp"
#include "thread_internal.hpp"
#include "virtual_memory.hpp"

#include <stdint.h>

namespace process {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) IDs survive process destruction
ProcessId g_next_process_id = 1;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

Process* create() {
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
    process->thread_head = nullptr;
    process->thread_tail = nullptr;
    process->thread_count = 0;
    return process;
}

bool destroy(Process* process) {
    if(process == nullptr || process->thread_count != 0 || process->state == State::Running ||
       virtual_memory::is_active(&process->address_space)) {
        return false;
    }

    virtual_memory::destroy_address_space(&process->address_space);
    if(process->address_space.root_physical != 0) {
        return false;
    }
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

State state(const Process* process) {
    return process == nullptr ? State::Terminated : process->state;
}

virtual_memory::AddressSpace* address_space(Process* process) {
    return process == nullptr ? nullptr : &process->address_space;
}

uint32_t thread_count(const Process* process) {
    return process == nullptr ? 0 : process->thread_count;
}

} // namespace process
