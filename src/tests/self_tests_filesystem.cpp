#include "ext2.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "vfs.hpp"
#include "virtio_block.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index,
//             hicpp-no-array-decay)
namespace self_tests_detail {

void test_filesystem() {
    auto* device = virtio_block::device();
    ext2::FileSystem file_system = {};
    if(device == nullptr || !ext2::mount(device, &file_system)) {
        panic::halt("filesystem smoke test could not mount ext2 image");
    }

    ext2::Inode hello = {};
    ext2::Inode config = {};
    ext2::Inode root = {};
    ext2::Inode temporary = {};
    ext2::Inode directory_tail = {};
    if(ext2::lookup(&file_system, "/hello.txt", &hello) != ext2::Status::Success || hello.directory ||
       hello.size == 0 || ext2::lookup(&file_system, "/etc/oscar/config.txt", &config) != ext2::Status::Success ||
       config.directory || config.size == 0 || ext2::lookup(&file_system, "/", &root) != ext2::Status::Success ||
       !root.directory || ext2::lookup(&file_system, "/missing", &temporary) != ext2::Status::NotFound ||
       ext2::lookup(&file_system, "/hello.txt/more", &temporary) != ext2::Status::NotDirectory ||
       ext2::lookup(&file_system, "///etc//./oscar/config.txt", &temporary) != ext2::Status::Success ||
       ext2::lookup(&file_system, "/many/file299", &directory_tail) != ext2::Status::Success ||
       directory_tail.directory) {
        panic::halt("filesystem smoke test returned an incorrect inode lookup result");
    }
    if(hello.uid != 0 || hello.gid != 0 || (hello.mode & 0777U) != 0644U || (root.mode & 0777U) != 0755U) {
        panic::halt("filesystem smoke test returned invalid inode ownership or permissions");
    }
    if(ext2::get_inode(&file_system, file_system.inode_count + 1, &root) != ext2::Status::Corrupt) {
        panic::halt("filesystem smoke test accepted an out-of-range inode");
    }

    constexpr char kExpectedHello[] = "Hello from the OScar filesystem.\n";
    constexpr char kExpectedConfig[] = "; ext2 lookup smoke-test data\n";
    char hello_contents[sizeof(kExpectedHello)] = {};
    char config_contents[sizeof(kExpectedConfig)] = {};
    uint32_t hello_bytes = 0;
    uint32_t config_bytes = 0;
    if(ext2::read_file(&file_system, &hello, 0, sizeof(hello_contents), hello_contents, &hello_bytes) !=
           ext2::Status::Success ||
       ext2::read_file(&file_system, &config, 0, sizeof(config_contents), config_contents, &config_bytes) !=
           ext2::Status::Success ||
       hello_bytes != sizeof(kExpectedHello) - 1 || config_bytes != sizeof(kExpectedConfig) - 1) {
        panic::halt("filesystem smoke test could not read file contents");
    }
    for(uint32_t index = 0; index < hello_bytes; ++index) {
        if(hello_contents[index] != kExpectedHello[index]) {
            panic::halt("filesystem smoke test read incorrect hello contents");
        }
    }
    for(uint32_t index = 0; index < config_bytes; ++index) {
        if(config_contents[index] != kExpectedConfig[index]) {
            panic::halt("filesystem smoke test read incorrect config contents");
        }
    }
    if(ext2::lookup(&file_system, "/", &root) != ext2::Status::Success) {
        panic::halt("filesystem smoke test could not reload the root inode");
    }
    uint32_t bytes_read = 0;
    if(ext2::read_file(&file_system, &hello, hello.size, 1, hello_contents, &bytes_read) != ext2::Status::Success ||
       bytes_read != 0 || ext2::read_file(&file_system, &hello, 0, 0, nullptr, &bytes_read) != ext2::Status::Success ||
       ext2::read_file(&file_system, &hello, 0, 1, nullptr, &bytes_read) != ext2::Status::InvalidArgument ||
       ext2::read_file(&file_system, &root, 0, 1, hello_contents, &bytes_read) != ext2::Status::NotDirectory) {
        panic::halt("filesystem smoke test accepted an invalid read request");
    }
    ext2::Inode forged = hello;
    forged.size = UINT64_MAX;
    if(ext2::read_file(&file_system, &forged, hello.size, 1, hello_contents, &bytes_read) != ext2::Status::Success ||
       bytes_read != 0) {
        panic::halt("filesystem smoke test trusted stale inode metadata");
    }

    ext2::Inode large = {};
    alignas(16) uint8_t boundary_contents[64] = {};
    if(ext2::lookup(&file_system, "/large.bin", &large) != ext2::Status::Success || large.directory ||
       large.size != 300ULL * 1024ULL) {
        panic::halt("filesystem smoke test could not find the large file fixture");
    }
    const uint64_t pointers_per_block = file_system.block_size / sizeof(uint32_t);
    const uint64_t single_boundary = 12ULL * file_system.block_size;
    const uint64_t double_boundary = (12ULL + pointers_per_block) * file_system.block_size;
    const uint64_t boundary_offsets[] = {single_boundary - 32, double_boundary - 32};
    for(const uint64_t offset : boundary_offsets) {
        uint32_t boundary_bytes = 0;
        if(ext2::read_file(
               &file_system, &large, offset, sizeof(boundary_contents), boundary_contents, &boundary_bytes
           ) != ext2::Status::Success ||
           boundary_bytes != sizeof(boundary_contents)) {
            panic::halt("filesystem smoke test could not cross an indirect block boundary");
        }
        for(const uint8_t byte : boundary_contents) {
            if(byte != 0) {
                panic::halt("filesystem smoke test read incorrect indirect-block data");
            }
        }
    }
    serial::write("Read-only ext2 mount, lookup, and file-read smoke test passed.\n");
}

void test_vfs() {
    if(!vfs::mount_root(virtio_block::device()) || !vfs::is_mounted()) {
        panic::halt("VFS smoke test could not mount the root filesystem");
    }
    vfs::FileSystemStatus filesystem_status = {};
    if(vfs::statfs(&filesystem_status) != vfs::Status::Success || filesystem_status.block_size != 1024 ||
       filesystem_status.total_blocks == 0 || filesystem_status.free_blocks == 0 ||
       filesystem_status.free_blocks >= filesystem_status.total_blocks || filesystem_status.total_inodes == 0 ||
       filesystem_status.free_inodes == 0 || filesystem_status.free_inodes >= filesystem_status.total_inodes) {
        panic::halt("VFS filesystem-status query returned invalid capacity counters");
    }

    vfs::File file = {};
    if(vfs::open("/hello.txt", vfs::kOpenRead, &file) != vfs::Status::Success) {
        panic::halt("VFS smoke test could not open a file");
    }
    char first_chunk[6] = {};
    uint32_t bytes_read = 0;
    if(vfs::read(&file, first_chunk, sizeof(first_chunk) - 1, &bytes_read) != vfs::Status::Success ||
       bytes_read != sizeof(first_chunk) - 1 || first_chunk[0] != 'H' || first_chunk[1] != 'e' ||
       first_chunk[2] != 'l' || first_chunk[3] != 'l' || first_chunk[4] != 'o') {
        panic::halt("VFS smoke test read the wrong first chunk");
    }
    if(vfs::seek(&file, file.node.size + 1) != vfs::Status::Success ||
       vfs::read(&file, nullptr, 0, &bytes_read) != vfs::Status::Success || bytes_read != 0 ||
       vfs::seek(&file, 0) != vfs::Status::Success ||
       vfs::read(&file, first_chunk, sizeof(first_chunk) - 1, &bytes_read) != vfs::Status::Success ||
       bytes_read != sizeof(first_chunk) - 1 || vfs::close(&file) != vfs::Status::Success || file.open) {
        panic::halt("VFS smoke test could not seek or close a file");
    }
    if(vfs::close(&file) != vfs::Status::InvalidArgument ||
       vfs::open("/hello.txt", 0, &file) != vfs::Status::InvalidArgument ||
       vfs::open("/hello.txt", 4, &file) != vfs::Status::InvalidArgument ||
       vfs::resolve("///etc//./oscar/config.txt", &file.node) != vfs::Status::Success) {
        panic::halt("VFS smoke test accepted an invalid handle or path request");
    }

    vfs::File directory = {};
    if(vfs::open("/", vfs::kOpenRead, &directory) != vfs::Status::Success ||
       vfs::read(&directory, first_chunk, sizeof(first_chunk), &bytes_read) != vfs::Status::IsDirectory ||
       vfs::close(&directory) != vfs::Status::Success ||
       vfs::resolve("/missing", &directory.node) != vfs::Status::NotFound) {
        panic::halt("VFS smoke test accepted an invalid operation");
    }

    const vfs::Credentials unprivileged = {.uid = 1000, .gid = 1000};
    vfs::File permission_file = {};
    if(vfs::open_as("/hello.txt", vfs::kOpenRead, &permission_file, unprivileged) != vfs::Status::Success ||
       vfs::close(&permission_file) != vfs::Status::Success ||
       vfs::open_as("/hello.txt", vfs::kOpenWrite, &permission_file, unprivileged) != vfs::Status::PermissionDenied ||
       vfs::read_directory_as("/", 0, nullptr, unprivileged) != vfs::Status::InvalidArgument) {
        panic::halt("VFS permission test returned an incorrect access result");
    }
    vfs::Node permission_node = {};
    if(vfs::create_as("/permission-denied", &permission_node, unprivileged) != vfs::Status::PermissionDenied) {
        panic::halt("VFS permission test allowed an unprivileged mutation");
    }
    serial::write("VFS mount, path, handle, read, seek, and close smoke test passed.\n");
}

void test_writable_filesystem() {
    constexpr uint32_t kWriteSize = 300 * 1024;
    static uint8_t write_buffer[kWriteSize];
    static uint8_t read_buffer[1024];
    static uint8_t hole_check[18];
    for(uint32_t index = 0; index < kWriteSize; ++index) {
        write_buffer[index] = static_cast<uint8_t>((index * 37U + 11U) & 0xffU);
    }

    vfs::File read_only = {};
    if(vfs::open("/hello.txt", vfs::kOpenRead, &read_only) != vfs::Status::Success) {
        panic::halt("writable filesystem smoke test could not open read-only fixture");
    }
    uint32_t bytes = 0;
    if(vfs::write(&read_only, write_buffer, 1, &bytes) != vfs::Status::InvalidArgument ||
       vfs::close(&read_only) != vfs::Status::Success) {
        panic::halt("writable filesystem smoke test allowed a read-only write");
    }

    vfs::File file = {};
    const vfs::Status open_status = vfs::open("/writable.txt", vfs::kOpenReadWrite, &file);
    const vfs::Status zero_write_status = vfs::write(&file, nullptr, 0, &bytes);
    const vfs::Status null_buffer_status = vfs::write(&file, nullptr, 1, &bytes);
    const uint8_t directory_byte = 0x5a;
    vfs::File directory = {};
    const vfs::Status directory_open_status = vfs::open("/", vfs::kOpenReadWrite, &directory);
    const vfs::Status directory_write_status = vfs::write(&directory, &directory_byte, 1, &bytes);
    const vfs::Status directory_close_status = vfs::close(&directory);
    const vfs::Status overflow_seek_status = vfs::seek(&file, UINT64_MAX);
    const vfs::Status overflow_write_status = vfs::write(&file, &directory_byte, 1, &bytes);
    const vfs::Status rewind_status = vfs::seek(&file, 0);
    const vfs::Status write_status = vfs::write(&file, write_buffer, kWriteSize, &bytes);
    if(open_status != vfs::Status::Success || zero_write_status != vfs::Status::Success ||
       null_buffer_status != vfs::Status::InvalidArgument || directory_open_status != vfs::Status::Success ||
       directory_write_status != vfs::Status::IsDirectory || directory_close_status != vfs::Status::Success ||
       overflow_seek_status != vfs::Status::Success || overflow_write_status != vfs::Status::Unsupported ||
       rewind_status != vfs::Status::Success || write_status != vfs::Status::Success || bytes != kWriteSize) {
        panic::halt("writable filesystem smoke test could not write the indirect-block fixture");
    }
    if(vfs::seek(&file, 0) != vfs::Status::Success) {
        panic::halt("writable filesystem smoke test could not rewind the file");
    }
    for(uint32_t offset = 0; offset < kWriteSize; offset += sizeof(read_buffer)) {
        const uint32_t expected =
            (kWriteSize - offset) < sizeof(read_buffer) ? kWriteSize - offset : sizeof(read_buffer);
        uint32_t received = 0;
        if(vfs::read(&file, read_buffer, expected, &received) != vfs::Status::Success || received != expected) {
            panic::halt("writable filesystem smoke test could not read back written data");
        }
        for(uint32_t index = 0; index < received; ++index) {
            if(read_buffer[index] != write_buffer[offset + index]) {
                panic::halt("writable filesystem smoke test read back corrupted data");
            }
        }
    }
    const uint8_t marker = 0xa7;
    if(vfs::seek(&file, kWriteSize + 17) != vfs::Status::Success ||
       vfs::write(&file, &marker, sizeof(marker), &bytes) != vfs::Status::Success || bytes != sizeof(marker) ||
       vfs::seek(&file, kWriteSize) != vfs::Status::Success ||
       vfs::read(&file, hole_check, sizeof(hole_check), &bytes) != vfs::Status::Success ||
       bytes != sizeof(hole_check)) {
        panic::halt("writable filesystem smoke test could not test a sparse extension");
    }
    for(uint32_t index = 0; index < sizeof(hole_check) - 1; ++index) {
        if(hole_check[index] != 0) {
            panic::halt("writable filesystem smoke test did not zero-fill an extension gap");
        }
    }
    if(hole_check[sizeof(hole_check) - 1] != marker || vfs::close(&file) != vfs::Status::Success) {
        panic::halt("writable filesystem smoke test returned an incorrect extension marker");
    }

    if(vfs::open("/writable.txt", vfs::kOpenRead, &file) != vfs::Status::Success || file.node.size != kWriteSize + 18 ||
       vfs::seek(&file, kWriteSize) != vfs::Status::Success ||
       vfs::read(&file, hole_check, sizeof(hole_check), &bytes) != vfs::Status::Success ||
       bytes != sizeof(hole_check) || vfs::close(&file) != vfs::Status::Success) {
        panic::halt("writable filesystem smoke test did not persist file metadata");
    }
    for(uint32_t index = 0; index < sizeof(hole_check) - 1; ++index) {
        if(hole_check[index] != 0) {
            panic::halt("writable filesystem smoke test did not persist zero-filled bytes");
        }
    }
    if(hole_check[sizeof(hole_check) - 1] != marker) {
        panic::halt("writable filesystem smoke test did not persist file data");
    }
    serial::write("Writable filesystem write, readback, indirect-block, and persistence smoke test passed.\n");
}

void test_filesystem_mutation() {
    // Clean up names left by an interrupted prior boot so the persistent test
    // image can be reused without turning an old partial run into a false
    // duplicate-entry failure.
    (void)vfs::unlink("/mutation.txt");
    (void)vfs::unlink("/mutation-dir/nested.txt");
    (void)vfs::rmdir("/mutation-dir");

    vfs::Node created = {};
    const vfs::Status create_status = vfs::create("/mutation.txt", &created);
    const vfs::Status duplicate_status = vfs::create("/mutation.txt", &created);
    const vfs::Status mkdir_status = vfs::mkdir("/mutation-dir");
    const vfs::Status duplicate_mkdir_status = vfs::mkdir("/mutation-dir");
    if(create_status != vfs::Status::Success || created.type != vfs::NodeType::Regular ||
       duplicate_status != vfs::Status::Exists || mkdir_status != vfs::Status::Success ||
       duplicate_mkdir_status != vfs::Status::Exists) {
        panic::halt("filesystem mutation smoke test could not create entries");
    }

    vfs::File file = {};
    constexpr char kMutationData[] = "created and persisted";
    char readback[sizeof(kMutationData)] = {};
    uint32_t transferred = 0;
    if(vfs::open("/mutation.txt", vfs::kOpenReadWrite, &file) != vfs::Status::Success ||
       vfs::write(&file, kMutationData, sizeof(kMutationData) - 1, &transferred) != vfs::Status::Success ||
       transferred != sizeof(kMutationData) - 1 || vfs::seek(&file, 0) != vfs::Status::Success ||
       vfs::read(&file, readback, sizeof(readback) - 1, &transferred) != vfs::Status::Success ||
       transferred != sizeof(kMutationData) - 1 || vfs::close(&file) != vfs::Status::Success) {
        panic::halt("filesystem mutation smoke test could not read a created file");
    }
    for(uint32_t index = 0; index < sizeof(kMutationData) - 1; ++index) {
        if(readback[index] != kMutationData[index]) {
            panic::halt("filesystem mutation smoke test read corrupted created data");
        }
    }

    const vfs::Status nested_create_status = vfs::create("/mutation-dir/nested.txt", &created);
    const vfs::Status nonempty_remove_status = vfs::rmdir("/mutation-dir");
    const vfs::Status nested_remove_status = vfs::unlink("/mutation-dir/nested.txt");
    const vfs::Status directory_remove_status = vfs::rmdir("/mutation-dir");
    const vfs::Status file_remove_status = vfs::unlink("/mutation.txt");
    const vfs::Status file_lookup_status = vfs::resolve("/mutation.txt", &created);
    const vfs::Status directory_lookup_status = vfs::resolve("/mutation-dir", &created);
    if(nested_create_status != vfs::Status::Success || nonempty_remove_status != vfs::Status::NotEmpty ||
       nested_remove_status != vfs::Status::Success || directory_remove_status != vfs::Status::Success ||
       file_remove_status != vfs::Status::Success || file_lookup_status != vfs::Status::NotFound ||
       directory_lookup_status != vfs::Status::NotFound) {
        panic::halt("filesystem mutation smoke test could not remove entries safely");
    }
    serial::write("Filesystem create, mkdir, unlink, rmdir, and metadata-reuse smoke test passed.\n");
}

void make_indexed_path(char* path, const char* prefix, uint32_t index) {
    uint32_t position = 0;
    while(prefix[position] != '\0') {
        path[position] = prefix[position];
        ++position;
    }
    path[position++] = '0' + static_cast<char>((index / 10) % 10);
    path[position++] = '0' + static_cast<char>(index % 10);
    path[position] = '\0';
}

void test_filesystem_edge_cases() {
    constexpr uint32_t kDirectoryStressCount = 80;
    constexpr uint32_t kLargeFileSize = 280 * 1024;
    static uint8_t large_file[kLargeFileSize];
    for(uint32_t index = 0; index < kLargeFileSize; ++index) {
        large_file[index] = static_cast<uint8_t>((index * 19U + 3U) & 0xffU);
    }

    char path[64];
    vfs::Node node = {};
    for(uint32_t index = 0; index < kDirectoryStressCount; ++index) {
        make_indexed_path(path, "/stress-file-", index);
        (void)vfs::unlink(path);
        if(vfs::create(path, &node) != vfs::Status::Success || node.type != vfs::NodeType::Regular) {
            panic::halt("filesystem edge-case test could not grow a directory");
        }
        vfs::File file = {};
        const uint8_t value = static_cast<uint8_t>(index);
        uint32_t transferred = 0;
        if(vfs::open(path, vfs::kOpenReadWrite, &file) != vfs::Status::Success ||
           vfs::write(&file, &value, sizeof(value), &transferred) != vfs::Status::Success ||
           transferred != sizeof(value) || vfs::close(&file) != vfs::Status::Success) {
            panic::halt("filesystem edge-case test could not write a stress file");
        }
    }
    for(uint32_t index = 0; index < kDirectoryStressCount; ++index) {
        make_indexed_path(path, "/stress-file-", index);
        if(vfs::resolve(path, &node) != vfs::Status::Success || vfs::unlink(path) != vfs::Status::Success) {
            panic::halt("filesystem edge-case test could not resolve and remove stress files");
        }
    }

    char long_path[258];
    long_path[0] = '/';
    for(uint32_t index = 1; index <= 255; ++index) {
        long_path[index] = 'x';
    }
    long_path[256] = '\0';
    (void)vfs::unlink(long_path);
    if(vfs::create(long_path, &node) != vfs::Status::Success || vfs::create(long_path, &node) != vfs::Status::Exists ||
       vfs::unlink(long_path) != vfs::Status::Success) {
        panic::halt("filesystem edge-case test rejected a maximum-length name");
    }
    long_path[256] = 'x';
    long_path[257] = '\0';
    if(vfs::create(long_path, &node) != vfs::Status::InvalidArgument ||
       vfs::create("/missing-parent/file", &node) != vfs::Status::NotFound ||
       vfs::create("/hello.txt/child", &node) != vfs::Status::NotDirectory ||
       vfs::create("/", &node) != vfs::Status::InvalidArgument) {
        panic::halt("filesystem edge-case test accepted an invalid path");
    }

    // Remove leftovers from an interrupted boot so this persistent-image test remains repeatable.
    (void)vfs::unlink("/type-check-dir/file");
    (void)vfs::rmdir("/type-check-dir");
    (void)vfs::unlink("/large-delete.bin");
    if(vfs::mkdir("/type-check-dir") != vfs::Status::Success ||
       vfs::unlink("/type-check-dir") != vfs::Status::IsDirectory ||
       vfs::rmdir("/hello.txt") != vfs::Status::NotDirectory ||
       vfs::create("/type-check-dir/file", &node) != vfs::Status::Success ||
       vfs::rmdir("/type-check-dir") != vfs::Status::NotEmpty ||
       vfs::unlink("/type-check-dir/file") != vfs::Status::Success ||
       vfs::rmdir("/type-check-dir") != vfs::Status::Success) {
        panic::halt("filesystem edge-case test accepted an invalid entry type operation");
    }

    if(vfs::create("/large-delete.bin", &node) != vfs::Status::Success) {
        panic::halt("filesystem edge-case test could not create an indirect-block file");
    }
    vfs::File large = {};
    uint32_t transferred = 0;
    if(vfs::open("/large-delete.bin", vfs::kOpenReadWrite, &large) != vfs::Status::Success ||
       vfs::write(&large, large_file, kLargeFileSize, &transferred) != vfs::Status::Success ||
       transferred != kLargeFileSize || vfs::close(&large) != vfs::Status::Success ||
       vfs::unlink("/large-delete.bin") != vfs::Status::Success ||
       vfs::resolve("/large-delete.bin", &node) != vfs::Status::NotFound) {
        panic::halt("filesystem edge-case test could not free indirect-block data");
    }

    serial::write("Filesystem directory-growth, path-validation, type-check, and block-reuse tests passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index,
//           hicpp-no-array-decay)
