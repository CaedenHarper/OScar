#include <oscar/stdio.h>
#include <oscar/syscalls.h>
#include <stdbool.h>
#include <stdint.h>

// NOLINTBEGIN(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//             cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//             cppcoreguidelines-pro-type-member-init)

static void fail(void) {
    oscar_write_string("shell command test failed.\n");
    oscar_exit(1);
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

static void expect_file_contents(const char* path, const char* expected, uint64_t expected_length) {
    const int64_t descriptor = oscar_open(path, OSCAR_OPEN_READ);
    expect(descriptor >= 0);

    char contents[64];
    const int64_t result = oscar_read(descriptor, contents, sizeof(contents));
    expect(result == (int64_t)expected_length);
    expect(bytes_equal(contents, expected, expected_length));
    expect(oscar_read(descriptor, contents, sizeof(contents)) == 0);
    expect(oscar_close(descriptor) == 0);
}

static void remove_old_fixture(void) {
    (void)oscar_unlink("/shell-test-moved");
    (void)oscar_unlink("/shell-test-copy");
    (void)oscar_unlink("/shell-test-source");
    (void)oscar_rmdir("/shell-test-dir");
}

void _start(void) {
    static const char kContents[] = "shell command data\n";
    static const char kDirectory[] = "/shell-test-dir";
    static const char kSource[] = "/shell-test-source";
    static const char kCopy[] = "/shell-test-copy";
    static const char kMoved[] = "/shell-test-moved";

    remove_old_fixture();

    // mkdir and touch are successful only when the requested filesystem nodes
    // are created with the expected types.
    expect(oscar_mkdir(kDirectory) == 0);
    struct oscar_stat status;
    expect(oscar_stat(kDirectory, &status) == 0);
    expect(status.type == OSCAR_NODE_DIRECTORY);
    expect(oscar_create(kSource) == 0);
    expect(oscar_stat(kSource, &status) == 0);
    expect(status.type == OSCAR_NODE_REGULAR);

    const int64_t source_descriptor = oscar_open(kSource, OSCAR_OPEN_WRITE);
    expect(source_descriptor >= 0);
    expect(oscar_write(source_descriptor, kContents, sizeof(kContents) - 1) == (int64_t)(sizeof(kContents) - 1));
    expect(oscar_close(source_descriptor) == 0);

    // cat's observable result is the exact byte stream read from the source.
    expect_file_contents(kSource, kContents, sizeof(kContents) - 1);

    expect(oscar_create(kCopy) == 0);
    const int64_t copy_input = oscar_open(kSource, OSCAR_OPEN_READ);
    const int64_t copy_output = oscar_open(kCopy, OSCAR_OPEN_WRITE);
    expect(copy_input >= 0 && copy_output >= 0);
    char buffer[64];
    const int64_t bytes_read = oscar_read(copy_input, buffer, sizeof(buffer));
    expect(bytes_read == (int64_t)(sizeof(kContents) - 1));
    expect(oscar_write(copy_output, buffer, (uint64_t)bytes_read) == bytes_read);
    expect(oscar_close(copy_input) == 0);
    expect(oscar_close(copy_output) == 0);
    expect_file_contents(kCopy, kContents, sizeof(kContents) - 1);

    expect(oscar_unlink(kSource) == 0);
    expect(oscar_open(kSource, OSCAR_OPEN_READ) == OSCAR_ERROR_NOT_FOUND);
    expect(oscar_create(kMoved) == 0);
    const int64_t move_input = oscar_open(kCopy, OSCAR_OPEN_READ);
    const int64_t move_output = oscar_open(kMoved, OSCAR_OPEN_WRITE);
    expect(move_input >= 0 && move_output >= 0);
    const int64_t moved_bytes = oscar_read(move_input, buffer, sizeof(buffer));
    expect(moved_bytes == (int64_t)(sizeof(kContents) - 1));
    expect(oscar_write(move_output, buffer, (uint64_t)moved_bytes) == moved_bytes);
    expect(oscar_close(move_input) == 0);
    expect(oscar_close(move_output) == 0);
    expect(oscar_unlink(kCopy) == 0);
    expect_file_contents(kMoved, kContents, sizeof(kContents) - 1);

    // ps is represented by the process-info API; find this process in its
    // snapshot and verify that the returned identity is internally consistent.
    const int64_t process_id = oscar_getpid();
    expect(process_id >= 0);
    bool found_process = false;
    for(uint64_t index = 0; index < 32; ++index) {
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

    expect(oscar_unlink(kMoved) == 0);
    expect(oscar_rmdir(kDirectory) == 0);
    oscar_write_string("shell command test passed; terminating with kill.\n");
    expect(oscar_kill((uint64_t)process_id) == 0);
    fail();
}

// NOLINTEND(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp,
//           cppcoreguidelines-pro-bounds-array-to-pointer-decay,
//           cppcoreguidelines-pro-type-member-init)
