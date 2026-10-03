#include <fcntl.h>
#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum {
    kFileContentsCapacity = 64,
    kMaximumProcessEntries = 32,
    kDirectoryMode = 0755,
    kFileMode = 0644,
    kWriteCreateFlags = O_WRONLY + O_CREAT,
};

__attribute__((noreturn)) static void fail(void) {
    oscar_write_string("shell command test failed.\n");
    _Exit(1);
}

static void expect(bool condition) {
    if(!condition) {
        fail();
    }
}

static bool bytes_equal(const char* left, const char* right, uint64_t length) {
    for(uint64_t index = 0; index < length; ++index) {
        if(left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void expect_file_contents(const char* path, const char* expected, uint64_t expected_length) {
    const int descriptor = open(path, O_RDONLY);
    expect(descriptor >= 0);

    char contents[kFileContentsCapacity];
    const ssize_t result = read(descriptor, contents, sizeof(contents));
    expect(result == (int64_t)expected_length);
    expect(bytes_equal(contents, expected, expected_length));
    expect(read(descriptor, contents, sizeof(contents)) == 0);
    expect(close(descriptor) == 0);
}

static void remove_old_fixture(void) {
    (void)unlink("/shell-test-moved");
    (void)unlink("/shell-test-copy");
    (void)unlink("/shell-test-source");
    (void)rmdir("/shell-test-dir");
}

int main(void) {
    static const char kContents[] = "shell command data\n";
    static const char kDirectory[] = "/shell-test-dir";
    static const char kSource[] = "/shell-test-source";
    static const char kCopy[] = "/shell-test-copy";
    static const char kMoved[] = "/shell-test-moved";

    remove_old_fixture();

    // mkdir and touch are successful only when the requested filesystem nodes
    // are created with the expected types.
    expect(mkdir(kDirectory, kDirectoryMode) == 0);
    struct stat status;
    expect(stat(kDirectory, &status) == 0);
    expect((status.st_mode & (uint32_t)S_IFMT) == (uint32_t)S_IFDIR);
    const int source_creation_descriptor = open(kSource, kWriteCreateFlags, kFileMode);
    expect(source_creation_descriptor >= 0);
    expect(close(source_creation_descriptor) == 0);
    expect(stat(kSource, &status) == 0);
    expect((status.st_mode & (uint32_t)S_IFMT) == (uint32_t)S_IFREG);

    const int source_descriptor = open(kSource, O_WRONLY);
    expect(source_descriptor >= 0);
    expect(write(source_descriptor, kContents, sizeof(kContents) - 1) == (ssize_t)(sizeof(kContents) - 1));
    expect(close(source_descriptor) == 0);

    // cat's observable result is the exact byte stream read from the source.
    expect_file_contents(kSource, kContents, sizeof(kContents) - 1);

    const int copy_output_creation_descriptor = open(kCopy, kWriteCreateFlags, kFileMode);
    expect(copy_output_creation_descriptor >= 0);
    expect(close(copy_output_creation_descriptor) == 0);
    const int copy_input = open(kSource, O_RDONLY);
    const int copy_output = open(kCopy, O_WRONLY);
    expect(copy_input >= 0 && copy_output >= 0);
    char buffer[kFileContentsCapacity];
    const ssize_t bytes_read = read(copy_input, buffer, sizeof(buffer));
    expect(bytes_read == (int64_t)(sizeof(kContents) - 1));
    expect(write(copy_output, buffer, (size_t)bytes_read) == bytes_read);
    expect(close(copy_input) == 0);
    expect(close(copy_output) == 0);
    expect_file_contents(kCopy, kContents, sizeof(kContents) - 1);

    expect(unlink(kSource) == 0);
    expect(open(kSource, O_RDONLY) < 0);
    const int move_output_creation_descriptor = open(kMoved, kWriteCreateFlags, kFileMode);
    expect(move_output_creation_descriptor >= 0);
    expect(close(move_output_creation_descriptor) == 0);
    const int move_input = open(kCopy, O_RDONLY);
    const int move_output = open(kMoved, O_WRONLY);
    expect(move_input >= 0 && move_output >= 0);
    const ssize_t moved_bytes = read(move_input, buffer, sizeof(buffer));
    expect(moved_bytes == (int64_t)(sizeof(kContents) - 1));
    expect(write(move_output, buffer, (size_t)moved_bytes) == moved_bytes);
    expect(close(move_input) == 0);
    expect(close(move_output) == 0);
    expect(unlink(kCopy) == 0);
    expect_file_contents(kMoved, kContents, sizeof(kContents) - 1);

    // ps is represented by the process-info API; find this process in its
    // snapshot and verify that the returned identity is internally consistent.
    const int64_t process_id = getpid();
    expect(process_id >= 0);
    bool found_process = false;
    for(uint64_t index = 0; index < kMaximumProcessEntries; ++index) {
        struct oscar_process_info info;
        const int64_t result = oscar_get_process_info(index, &info);
        if(result == OSCAR_ERROR_NOT_FOUND) {
            break;
        }
        expect(result >= 0);
        if(info.id == (uint64_t)process_id) {
            found_process = true;
            break;
        }
    }
    expect(found_process);

    expect(unlink(kMoved) == 0);
    expect(rmdir(kDirectory) == 0);
    oscar_write_string("shell command test passed; terminating with kill.\n");
    expect(kill((pid_t)process_id, SIGTERM) == 0);
    fail();
}
