#include "ext2.hpp"
#include "panic.hpp"
#include "self_tests_internal.hpp"
#include "serial.hpp"
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
    serial::write("Read-only ext2 mount, lookup, and file-read smoke test passed.\n");
}

} // namespace self_tests_detail

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index,
//           hicpp-no-array-decay)
