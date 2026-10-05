#pragma once

#include "tcp_connection.hpp"
#include "vfs.hpp"
#include "wait_queue.hpp"

#include <stdint.h>

namespace descriptor {

constexpr uint32_t kMaximumCount = 16;
constexpr uint32_t kFirstAllocatable = 3;
constexpr uint32_t kStandardInput = 0;
constexpr uint32_t kStandardOutput = 1;
constexpr uint32_t kStandardError = 2;
constexpr uint32_t kPipeCapacity = 4096;

enum class Kind : uint8_t {
    Invalid,
    StandardInput,
    StandardOutput,
    StandardError,
    File,
    PipeRead,
    PipeWrite,
    Socket,
};

struct Pipe {
    uint8_t buffer[kPipeCapacity];
    uint32_t read_position;
    uint32_t write_position;
    uint32_t bytes;
    uint32_t readers;
    uint32_t writers;
    synchronization::WaitQueue read_waiters;
    synchronization::WaitQueue write_waiters;
};

struct Socket {
    tcp_connection::Connection connection;
    uint32_t references;
};

struct Entry {
    Kind kind;
    vfs::File file;
    Pipe* pipe;
    Socket* socket;
    bool open;
};

struct Table {
    Entry entries[kMaximumCount];
};

/** Initialize an empty table with the three standard terminal descriptors. */
void initialize(Table* table);

/** Copy all inherited entries and acquire references to shared resources. */
bool inherit(Table* child, const Table* parent);

/** Allocate the lowest unused descriptor for an open VFS file. */
int32_t allocate_file(Table* table, const vfs::File* file);

/** Create a reference-counted in-memory pipe and return its read/write descriptors. */
bool create_pipe(Table* table, int64_t descriptors[2]);

/** Allocate the lowest unused descriptor for a reference-counted TCP socket. */
int32_t allocate_socket(Table* table, Socket* socket);

/** Duplicate a descriptor into target, closing the previous target when open. */
int32_t duplicate(Table* table, uint64_t descriptor, uint64_t target);

/** Return the descriptor kind, or Invalid for an unavailable descriptor. */
Kind kind(const Table* table, uint64_t descriptor);

/** Return a pipe endpoint and optionally its read/write kind. */
Pipe* pipe(Table* table, uint64_t descriptor, Kind* kind);

/** Return a process-owned TCP socket descriptor, or null for another kind. */
Socket* socket(Table* table, uint64_t descriptor);

/** Return a writable pointer to a regular-file descriptor, or null for other kinds. */
vfs::File* file(Table* table, uint64_t descriptor);

/** Close one descriptor and release its underlying resource. */
bool close(Table* table, uint64_t descriptor);

/** Close a descriptor, allowing the final socket reference to perform a graceful teardown. */
bool close(Table* table, uint64_t descriptor, uint64_t socket_timeout_ticks);

/** Close every open descriptor in a table. */
void close_all(Table* table);

} // namespace descriptor
