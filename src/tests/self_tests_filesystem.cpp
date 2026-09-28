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
    ext2::Inode directory_tail = {};
    if(ext2::lookup(&file_system, "/hello.txt", &hello) != ext2::Status::Success || hello.directory ||
       hello.size == 0 || ext2::lookup(&file_system, "/etc/oscar/config.txt", &config) != ext2::Status::Success ||
       config.directory || config.size == 0 || ext2::lookup(&file_system, "/", &root) != ext2::Status::Success ||
       !root.directory || ext2::lookup(&file_system, "/missing", &root) != ext2::Status::NotFound ||
       ext2::lookup(&file_system, "/hello.txt/more", &root) != ext2::Status::NotDirectory ||
       ext2::lookup(&file_system, "///etc//./oscar/config.txt", &root) != ext2::Status::Success ||
       ext2::lookup(&file_system, "/many/file299", &directory_tail) != ext2::Status::Success ||
       directory_tail.directory) {
        panic::halt("filesystem smoke test returned an incorrect inode lookup result");
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

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index,
//           hicpp-no-array-decay)
