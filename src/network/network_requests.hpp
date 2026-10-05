#pragma once

#include "arp.hpp"
#include "tcp_connection.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace network_requests {

constexpr uint16_t kRequestBufferSize = 4096;

enum class Type : uint8_t {
    Connect,
    Send,
    Receive,
    Close,
};

enum class State : uint8_t {
    Queued,
    Completed,
    Cancelled,
};

struct Request {
    Request* next;
    Type type;
    State state;
    tcp_connection::Connection* connection;
    arp::Ipv4Address destination;
    uint16_t destination_port;
    uint64_t deadline;
    uint16_t requested;
    uint16_t transferred;
    uint16_t offset;
    int32_t result;
    bool started;
    bool registered;
    uint8_t buffer[kRequestBufferSize];
    uint8_t references;
    synchronization::WaitQueue completion_waiters;
};

/** Initialize the request queue and service-thread wait queue. */
void initialize();

/** Allocate a request with one owner reference and one service reference. */
Request* allocate(Type type);

/** Retain a request reference while it remains reachable by another owner. */
void retain(Request* request);

/** Release a request reference and reclaim it after the final owner leaves. */
void release(Request* request);

/** Append a request for service-thread processing. */
bool enqueue(Request* request);

/** Remove one request from the service queue, or return null when it is empty. */
Request* dequeue();

/** Return whether at least one request is queued. */
bool has_pending();

/** Complete a request and wake all threads waiting for its result. */
void complete(Request* request, State state, int32_t result, uint16_t transferred);

/** Block the caller until the service completes or cancels the request. */
void wait(Request* request);

/** Block the network service until new work arrives. */
void wait_for_work();

} // namespace network_requests
