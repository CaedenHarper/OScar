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
    if(ext2::lookup(&file_system, "/hello.txt", &hello) != ext2::Status::Success || hello.directory ||
       hello.size == 0 || ext2::lookup(&file_system, "/etc/oscar/config.txt", &config) != ext2::Status::Success ||
       config.directory || config.size == 0 || ext2::lookup(&file_system, "/", &root) != ext2::Status::Success ||
       !root.directory || ext2::lookup(&file_system, "/missing", &root) != ext2::Status::NotFound ||
       ext2::lookup(&file_system, "/hello.txt/more", &root) != ext2::Status::NotDirectory) {
        panic::halt("filesystem smoke test returned an incorrect inode lookup result");
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
    if(vfs::seek(&file, 0) != vfs::Status::Success ||
       vfs::read(&file, first_chunk, sizeof(first_chunk) - 1, &bytes_read) != vfs::Status::Success ||
       bytes_read != sizeof(first_chunk) - 1 || vfs::close(&file) != vfs::Status::Success || file.open) {
        panic::halt("VFS smoke test could not seek or close a file");
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

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index,
//           hicpp-no-array-decay)
