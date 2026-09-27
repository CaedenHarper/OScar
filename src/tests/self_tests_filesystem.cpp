#include "ext2.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
#include "virtio_block.hpp"

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
    serial::write("Read-only ext2 mount and inode lookup smoke test passed.\n");
}

} // namespace self_tests_detail
