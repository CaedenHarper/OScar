#pragma once

#include "process.hpp"
#include "syscalls.hpp"
#include "vfs.hpp"

#include <stdint.h>

namespace syscall_detail {

constexpr uint64_t kWrite = 0;
constexpr uint64_t kExit = 1;
constexpr uint64_t kYield = 2;
constexpr uint64_t kSleep = 3;
constexpr uint64_t kGetPid = 4;
constexpr uint64_t kGetId = 5;
constexpr uint64_t kOpen = 6;
constexpr uint64_t kRead = 7;
constexpr uint64_t kClose = 8;
constexpr uint64_t kSeek = 9;
constexpr uint64_t kSpawn = 10;
constexpr uint64_t kWaitPid = 11;
constexpr uint64_t kCreate = 12;
constexpr uint64_t kMkdir = 13;
constexpr uint64_t kUnlink = 14;
constexpr uint64_t kRmdir = 15;
constexpr uint64_t kChdir = 16;
constexpr uint64_t kGetcwd = 17;
constexpr uint64_t kStat = 18;
constexpr uint64_t kReaddir = 19;
constexpr uint64_t kGetProcessInfo = 20;
constexpr uint64_t kKill = 21;
constexpr uint64_t kDup = 23;
constexpr uint64_t kDup2 = 24;
constexpr uint64_t kPipe = 25;
constexpr uint64_t kStatfs = 26;
constexpr uint64_t kPing = 27;
constexpr uint64_t kResolve = 28;
constexpr uint64_t kSocket = 29;
constexpr uint64_t kConnect = 30;
constexpr uint64_t kSend = 31;
constexpr uint64_t kRecv = 32;
#ifdef OSCAR_TEST_SUITE
constexpr uint64_t kTestComplete = 22;
constexpr uint16_t kTestExitPort = 0xf4;
constexpr uint8_t kTestExitCode = 0x10;
#endif

constexpr uint64_t kMaximumWriteLength = 4096;
constexpr uint64_t kMaximumReadLength = 4096;
constexpr uint64_t kMaximumPathLength = 511;
constexpr uint64_t kMaximumNameLength = 255;
constexpr uint64_t kMaximumHostnameLength = 253;
constexpr uint64_t kMaximumPathComponents = 256;
constexpr uint64_t kReadBufferSize = 128;
constexpr uint64_t kMillisecondsPerSecond = 1000;
constexpr uint64_t kSocketTimeoutTicks = 100;
constexpr uint64_t kMaximumSocketTransfer = 4096;
constexpr uint64_t kAddressFamilyIpv4 = 2;
constexpr uint64_t kSocketTypeStream = 1;
constexpr uint64_t kProtocolTcp = 6;
constexpr int64_t kErrorInvalidArgument = -1;
constexpr int64_t kErrorUnknownCall = -2;
constexpr int64_t kErrorNotFound = -3;
constexpr int64_t kErrorBadDescriptor = -4;
constexpr int64_t kErrorIsDirectory = -5;
constexpr int64_t kErrorIo = -6;
constexpr int64_t kErrorReadOnly = -7;
constexpr int64_t kErrorExists = -8;
constexpr int64_t kErrorNotEmpty = -9;
constexpr int64_t kErrorNoSpace = -10;
constexpr int64_t kErrorBufferTooSmall = -11;
constexpr int64_t kErrorPermissionDenied = -12;
constexpr int64_t kErrorNetworkUnavailable = -13;
constexpr int64_t kErrorNetworkTimeout = -14;
constexpr int64_t kErrorAddressUnreachable = -15;
constexpr int64_t kErrorNameNotFound = -16;
constexpr int64_t kErrorConnectionReset = -17;
constexpr int64_t kErrorNotConnected = -18;
constexpr int64_t kKillExitStatus = 137;

struct UserStat {
    uint64_t size;
    uint32_t type;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t reserved;
};

struct UserFileSystemStatus {
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    char filesystem[vfs::kFilesystemNameCapacity];
    char device[vfs::kDeviceNameCapacity];
    char mount_point[vfs::kMountPointCapacity];
};

struct UserDirectoryEntry {
    uint64_t identifier;
    uint64_t size;
    uint32_t type;
    uint32_t name_length;
    char name[kMaximumNameLength + 1];
};

struct UserProcessInfo {
    uint64_t id;
    uint32_t state;
    uint32_t thread_count;
    uint64_t user_page_count;
    char image_path[process::kMaximumImagePathLength + 1];
};

using PathOperation = vfs::Status (*)(const char* path, vfs::Credentials credentials);

process::Process* current_process();
int64_t translate_vfs_status(vfs::Status status);
bool copy_path(uintptr_t user_path, char* path);
bool copy_spawn_arguments(uintptr_t user_arguments, char* storage, const char** arguments, uint32_t* argument_count);
bool normalize_process_path(const process::Process* owner, const char* input, char* output);
bool copy_process_path(const syscalls::Frame* frame, const process::Process* owner, char* path);
int64_t write_pipe(const syscalls::Frame* frame, process::Process* owner);
int64_t read_pipe(const syscalls::Frame* frame, process::Process* owner);

int64_t write(const syscalls::Frame* frame);
int64_t get_pid();
int64_t get_id();
int64_t ping(const syscalls::Frame* frame);
int64_t resolve_hostname(const syscalls::Frame* frame);
int64_t get_process_info(const syscalls::Frame* frame);
int64_t kill_process(const syscalls::Frame* frame);
int64_t spawn(const syscalls::Frame* frame);
int64_t wait_pid(const syscalls::Frame* frame);

int64_t open(const syscalls::Frame* frame);
int64_t create_file(const syscalls::Frame* frame);
int64_t path_operation(const syscalls::Frame* frame, PathOperation operation);
int64_t change_directory(const syscalls::Frame* frame);
int64_t get_working_directory(const syscalls::Frame* frame);
int64_t stat_path(const syscalls::Frame* frame);
int64_t stat_filesystem(const syscalls::Frame* frame);
int64_t read_directory(const syscalls::Frame* frame);
int64_t seek(const syscalls::Frame* frame);

int64_t read(const syscalls::Frame* frame);
int64_t duplicate(const syscalls::Frame* frame, bool explicit_target);
int64_t create_pipe(const syscalls::Frame* frame);
int64_t close(const syscalls::Frame* frame);

int64_t socket_create(const syscalls::Frame* frame);
int64_t socket_connect(const syscalls::Frame* frame);
int64_t socket_send(const syscalls::Frame* frame);
int64_t socket_receive(const syscalls::Frame* frame);

#ifdef OSCAR_TEST_SUITE
[[noreturn]] void complete_test_suite();
#endif

} // namespace syscall_detail
