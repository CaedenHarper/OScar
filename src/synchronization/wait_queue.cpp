#include "wait_queue.hpp"

#include "thread.hpp"
#include "thread_internal.hpp"

namespace synchronization {

void initialize(WaitQueue* queue) {
    if(queue != nullptr) {
        queue->head = nullptr;
        queue->tail = nullptr;
    }
}

bool empty(const WaitQueue* queue) {
    return queue == nullptr || queue->head == nullptr;
}

bool enqueue_locked(WaitQueue* queue, kernel_thread::Thread* thread) {
    if(queue == nullptr || thread == nullptr || thread->state != kernel_thread::State::Running || thread->queued ||
       thread->waiting) {
        return false;
    }

    thread->waiting_next = nullptr;
    thread->waiting = true;
    thread->wait_queue = queue;
    if(queue->tail == nullptr) {
        queue->head = thread;
        queue->tail = thread;
    } else {
        queue->tail->waiting_next = thread;
        queue->tail = thread;
    }
    return true;
}

kernel_thread::Thread* dequeue_locked(WaitQueue* queue) {
    if(queue == nullptr || queue->head == nullptr) {
        return nullptr;
    }

    auto* thread = queue->head;
    queue->head = thread->waiting_next;
    if(queue->head == nullptr) {
        queue->tail = nullptr;
    }
    thread->waiting_next = nullptr;
    thread->waiting = false;
    thread->wait_queue = nullptr;
    return thread;
}

bool remove_locked(WaitQueue* queue, kernel_thread::Thread* thread) {
    if(queue == nullptr || thread == nullptr || !thread->waiting || thread->wait_queue != queue) {
        return false;
    }

    kernel_thread::Thread* previous = nullptr;
    for(auto* candidate = queue->head; candidate != nullptr; candidate = candidate->waiting_next) {
        if(candidate != thread) {
            previous = candidate;
            continue;
        }

        if(previous == nullptr) {
            queue->head = candidate->waiting_next;
        } else {
            previous->waiting_next = candidate->waiting_next;
        }
        if(queue->tail == candidate) {
            queue->tail = previous;
        }
        candidate->waiting_next = nullptr;
        candidate->waiting = false;
        candidate->wait_queue = nullptr;
        return true;
    }
    return false;
}

} // namespace synchronization
