#include "ext2.hpp"

#include <stdint.h>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//             readability-magic-numbers, bugprone-easily-swappable-parameters)

namespace ext2 {

namespace {

constexpr uint16_t kSuperblockMagic = 0xef53;
constexpr uint32_t kDirectoryType = 0x4000;
constexpr uint32_t kDirectoryEntryFileTypeFeature = 0x2;
constexpr uint32_t kBlockSizeMinimum = 1024;
constexpr uint32_t kBlockSizeMaximum = 4096;
constexpr uint32_t kMaximumGroups = 1024;
constexpr uint32_t kSuperblockOffset = 1024;
constexpr uint32_t kSuperblockSize = 1024;
constexpr uint32_t kDirectoryEntryHeaderSize = 8;
constexpr uint32_t kDirectBlockCount = 12;
constexpr uint32_t kIndirectBlockCount = 15;

enum class DataBlockResult : uint8_t {
    Success,
    Hole,
    Corrupt,
    IoError,
    Unsupported,
};

alignas(4096) uint8_t g_io_buffer[kBlockSizeMaximum];

uint16_t read_u16(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

uint32_t read_u32(const uint8_t* bytes, uint32_t offset) {
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16U) | (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

bool read_bytes(const block_device::Device* device, uint64_t byte_offset, uint32_t length) {
    if(device == nullptr || length == 0 || length > sizeof(g_io_buffer)) {
        return false;
    }
    const auto geometry = block_device::get_geometry(device);
    if(geometry.block_size == 0 || byte_offset > UINT64_MAX - length) {
        return false;
    }
    const uint64_t first_block = byte_offset / geometry.block_size;
    const uint64_t offset = byte_offset % geometry.block_size;
    const uint64_t block_count = (offset + length + geometry.block_size - 1) / geometry.block_size;
    if(block_count > UINT32_MAX || block_count * geometry.block_size > sizeof(g_io_buffer)) {
        return false;
    }
    // The block-device read callback does not mutate the device description; the protocol
    // predates const-qualified read handles, so only the API boundary needs this cast.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return block_device::read(
               const_cast<block_device::Device*>(device), first_block, static_cast<uint32_t>(block_count), g_io_buffer
           ) == block_device::Status::Success;
}

bool read_fs_block(const FileSystem* file_system, uint32_t block) {
    if(file_system == nullptr || block >= file_system->block_count) {
        return false;
    }
    return read_bytes(
        file_system->device, static_cast<uint64_t>(block) * file_system->block_size, file_system->block_size
    );
}

bool valid_inode_number(const FileSystem* file_system, uint32_t inode_number) {
    return file_system != nullptr && inode_number != 0 &&
           inode_number <= file_system->group_count * file_system->inodes_per_group;
}

bool read_inode(const FileSystem* file_system, uint32_t inode_number, Inode* inode, uint32_t* direct_blocks = nullptr) {
    if(!valid_inode_number(file_system, inode_number) || inode == nullptr) {
        return false;
    }
    const uint32_t group = (inode_number - 1) / file_system->inodes_per_group;
    const uint32_t index = (inode_number - 1) % file_system->inodes_per_group;
    const uint32_t descriptor_block =
        (file_system->block_size == 1024 ? 2 : 1) + (group * 32) / file_system->block_size;
    const uint32_t descriptor_offset = (group * 32) % file_system->block_size;
    if(!read_fs_block(file_system, descriptor_block) || descriptor_offset + 12 > file_system->block_size) {
        return false;
    }
    const uint32_t inode_table = read_u32(g_io_buffer + descriptor_offset, 8);
    const uint64_t inode_offset = static_cast<uint64_t>(index) * file_system->inode_size;
    const uint32_t table_block = inode_table + static_cast<uint32_t>(inode_offset / file_system->block_size);
    const uint32_t table_offset = static_cast<uint32_t>(inode_offset % file_system->block_size);
    if(table_block >= file_system->block_count || table_offset + file_system->inode_size > file_system->block_size ||
       !read_fs_block(file_system, table_block)) {
        return false;
    }

    const uint8_t* raw_inode = g_io_buffer + table_offset;
    inode->number = inode_number;
    inode->mode = read_u16(raw_inode, 0);
    inode->size = read_u32(raw_inode, 4);
    if((inode->mode & kDirectoryType) == 0 && (inode->mode & 0xf000U) == 0) {
        return false;
    }
    inode->directory = (inode->mode & 0xf000U) == kDirectoryType;
    if(!inode->directory) {
        inode->size |= static_cast<uint64_t>(read_u32(raw_inode, 108)) << 32U;
    }
    if(direct_blocks != nullptr) {
        for(uint32_t index = 0; index < kIndirectBlockCount; ++index) {
            direct_blocks[index] = read_u32(raw_inode, 40 + index * sizeof(uint32_t));
        }
    }
    return true;
}

DataBlockResult get_data_block(
    const FileSystem* file_system,
    const uint32_t* inode_blocks,
    uint64_t logical_block,
    uint32_t* physical_block
) {
    if(file_system == nullptr || inode_blocks == nullptr || physical_block == nullptr) {
        return DataBlockResult::Corrupt;
    }
    const uint64_t pointers_per_block = file_system->block_size / sizeof(uint32_t);
    if(logical_block < kDirectBlockCount) {
        *physical_block = inode_blocks[logical_block];
    } else {
        logical_block -= kDirectBlockCount;
        if(logical_block < pointers_per_block) {
            if(inode_blocks[12] == 0) {
                return DataBlockResult::Hole;
            }
            if(!read_fs_block(file_system, inode_blocks[12])) {
                return DataBlockResult::IoError;
            }
            *physical_block = read_u32(g_io_buffer, static_cast<uint32_t>(logical_block * sizeof(uint32_t)));
        } else {
            logical_block -= pointers_per_block;
            const uint64_t double_capacity = pointers_per_block * pointers_per_block;
            if(logical_block >= double_capacity) {
                return DataBlockResult::Unsupported;
            }
            if(inode_blocks[13] == 0) {
                return DataBlockResult::Hole;
            }
            const uint32_t first_index = static_cast<uint32_t>(logical_block / pointers_per_block);
            const uint32_t second_index = static_cast<uint32_t>(logical_block % pointers_per_block);
            if(!read_fs_block(file_system, inode_blocks[13])) {
                return DataBlockResult::IoError;
            }
            const uint32_t indirect_block = read_u32(g_io_buffer, first_index * sizeof(uint32_t));
            if(indirect_block == 0) {
                return DataBlockResult::Hole;
            }
            if(!read_fs_block(file_system, indirect_block)) {
                return DataBlockResult::IoError;
            }
            *physical_block = read_u32(g_io_buffer, second_index * sizeof(uint32_t));
        }
    }
    if(*physical_block == 0) {
        return DataBlockResult::Hole;
    }
    return *physical_block < file_system->block_count ? DataBlockResult::Success : DataBlockResult::Corrupt;
}

bool component_equals(const uint8_t* name, uint8_t name_length, const char* component, uint32_t component_length) {
    if(name_length != component_length) {
        return false;
    }
    for(uint32_t index = 0; index < component_length; ++index) {
        if(name[index] != static_cast<uint8_t>(component[index])) {
            return false;
        }
    }
    return true;
}

Status find_child(
    const FileSystem* file_system,
    const Inode& directory,
    const char* component,
    uint32_t length,
    Inode* child
) {
    uint32_t inode_blocks[kIndirectBlockCount] = {};
    Inode ignored = {};
    if(!read_inode(file_system, directory.number, &ignored, inode_blocks)) {
        return Status::Corrupt;
    }
    for(uint32_t block_index = 0; block_index < kDirectBlockCount; ++block_index) {
        if(inode_blocks[block_index] == 0) {
            continue;
        }
        if(!read_fs_block(file_system, inode_blocks[block_index])) {
            return Status::IoError;
        }
        uint32_t offset = 0;
        const uint32_t block_limit =
            directory.size > static_cast<uint64_t>(block_index) * file_system->block_size
                ? static_cast<uint32_t>(directory.size - static_cast<uint64_t>(block_index) * file_system->block_size)
                : 0;
        const uint32_t available = block_limit < file_system->block_size ? block_limit : file_system->block_size;
        while(offset + kDirectoryEntryHeaderSize <= available) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 || entry_size > available - offset ||
               name_length > entry_size - kDirectoryEntryHeaderSize) {
                return Status::Corrupt;
            }
            if(entry_inode != 0 &&
               component_equals(g_io_buffer + offset + kDirectoryEntryHeaderSize, name_length, component, length)) {
                return read_inode(file_system, entry_inode, child) ? Status::Success : Status::Corrupt;
            }
            offset += entry_size;
        }
        if(offset != available && available != 0) {
            return Status::Corrupt;
        }
    }
    return Status::NotFound;
}

bool next_component(const char** path, const char** component, uint32_t* length) {
    while(**path == '/') {
        ++*path;
    }
    if(**path == '\0') {
        return false;
    }
    *component = *path;
    uint32_t count = 0;
    while((*path)[count] != '\0' && (*path)[count] != '/') {
        ++count;
    }
    *length = count;
    *path += count;
    return true;
}

} // namespace

bool mount(block_device::Device* device, FileSystem* file_system) {
    if(device == nullptr || file_system == nullptr || device->geometry.block_size == 0 ||
       device->geometry.block_size > kBlockSizeMaximum || kSuperblockOffset % device->geometry.block_size != 0) {
        return false;
    }
    *file_system = {};
    if(!read_bytes(device, kSuperblockOffset, kSuperblockSize) ||
       read_u16(g_io_buffer + (kSuperblockOffset % device->geometry.block_size), 56) != kSuperblockMagic) {
        return false;
    }
    const uint8_t* superblock = g_io_buffer + (kSuperblockOffset % device->geometry.block_size);
    const uint32_t log_block_size = read_u32(superblock, 24);
    if(log_block_size > 2) {
        return false;
    }
    const uint32_t block_size = kBlockSizeMinimum << log_block_size;
    const uint32_t blocks_count = read_u32(superblock, 4);
    const uint32_t first_data_block = read_u32(superblock, 20);
    const uint32_t blocks_per_group = read_u32(superblock, 32);
    const uint32_t inodes_per_group = read_u32(superblock, 40);
    const uint32_t inode_count = read_u32(superblock, 0);
    const uint32_t inode_size = read_u16(superblock, 88);
    const uint32_t incompatible = read_u32(superblock, 96);
    if(block_size < kBlockSizeMinimum || block_size > kBlockSizeMaximum ||
       block_size % device->geometry.block_size != 0 || blocks_count == 0 ||
       blocks_count > device->geometry.block_count * device->geometry.block_size / block_size ||
       blocks_per_group == 0 || inodes_per_group == 0 || inode_count == 0 || inode_size < 128 ||
       inode_size > block_size || block_size % inode_size != 0 ||
       (incompatible & ~kDirectoryEntryFileTypeFeature) != 0) {
        return false;
    }
    const uint32_t group_count = (blocks_count - first_data_block + blocks_per_group - 1) / blocks_per_group;
    const uint32_t inode_table_blocks = (inodes_per_group * inode_size + block_size - 1) / block_size;
    if(first_data_block >= blocks_count || group_count == 0 || group_count > kMaximumGroups ||
       inode_table_blocks == 0) {
        return false;
    }
    file_system->device = device;
    file_system->block_count = blocks_count;
    file_system->block_size = block_size;
    file_system->inodes_per_group = inodes_per_group;
    file_system->inode_size = inode_size;
    file_system->group_count = group_count;
    file_system->inode_table_blocks = inode_table_blocks;
    file_system->mounted = true;
    synchronization::initialize(&file_system->io_lock);

    Inode root = {};
    if(!read_inode(file_system, 2, &root) || !root.directory) {
        *file_system = {};
        return false;
    }
    return true;
}

Status lookup(const FileSystem* file_system, const char* path, Inode* inode) {
    if(file_system == nullptr || !file_system->mounted || path == nullptr || inode == nullptr || *path == '\0') {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    Inode current = {};
    if(!read_inode(file_system, 2, &current) || !current.directory) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Corrupt;
    }
    const char* remaining = path;
    const char* component = nullptr;
    uint32_t length = 0;
    while(next_component(&remaining, &component, &length)) {
        if(length == 1 && component[0] == '.') {
            continue;
        }
        if(length == 2 && component[0] == '.' && component[1] == '.') {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Unsupported;
        }
        if(!current.directory) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::NotDirectory;
        }
        const Status result = find_child(file_system, current, component, length, &current);
        if(result != Status::Success) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return result;
        }
    }
    *inode = current;
    synchronization::unlock(&file_system->io_lock, previous_state);
    return Status::Success;
}

Status get_inode(const FileSystem* file_system, uint32_t inode_number, Inode* inode) {
    if(file_system == nullptr || !file_system->mounted || inode == nullptr) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const bool loaded = read_inode(file_system, inode_number, inode);
    synchronization::unlock(&file_system->io_lock, previous_state);
    return loaded ? Status::Success : Status::Corrupt;
}

Status lookup_child(const FileSystem* file_system, const Inode* directory, const char* name, Inode* inode) {
    if(file_system == nullptr || !file_system->mounted || directory == nullptr || name == nullptr || inode == nullptr ||
       !directory->directory || *name == '\0') {
        return Status::InvalidArgument;
    }
    uint32_t length = 0;
    while(name[length] != '\0') {
        ++length;
    }
    if(length > UINT8_MAX) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const Status result = find_child(file_system, *directory, name, length, inode);
    synchronization::unlock(&file_system->io_lock, previous_state);
    return result;
}

Status read_file(
    const FileSystem* file_system,
    const Inode* inode,
    uint64_t offset,
    uint32_t length,
    void* buffer,
    uint32_t* bytes_read
) {
    if(file_system == nullptr || !file_system->mounted || inode == nullptr || bytes_read == nullptr ||
       (length != 0 && buffer == nullptr)) {
        return Status::InvalidArgument;
    }
    *bytes_read = 0;
    if(inode->directory) {
        return Status::NotDirectory;
    }
    if(offset >= inode->size || length == 0) {
        return Status::Success;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const uint64_t available = inode->size - offset;
    const uint32_t requested = available < length ? static_cast<uint32_t>(available) : length;
    uint32_t inode_blocks[kIndirectBlockCount] = {};
    Inode verified = {};
    if(!read_inode(file_system, inode->number, &verified, inode_blocks)) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Corrupt;
    }

    uint8_t* destination = static_cast<uint8_t*>(buffer);
    uint64_t position = offset;
    while(*bytes_read < requested) {
        const uint64_t logical_block = position / file_system->block_size;
        uint32_t physical_block = 0;
        const DataBlockResult block_result = get_data_block(file_system, inode_blocks, logical_block, &physical_block);
        if(block_result == DataBlockResult::Unsupported) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Unsupported;
        }
        if(block_result == DataBlockResult::Corrupt || block_result == DataBlockResult::IoError) {
            const Status result = block_result == DataBlockResult::Corrupt ? Status::Corrupt : Status::IoError;
            synchronization::unlock(&file_system->io_lock, previous_state);
            return result;
        }
        if(block_result == DataBlockResult::Success && !read_fs_block(file_system, physical_block)) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::IoError;
        }
        const uint32_t block_offset = static_cast<uint32_t>(position % file_system->block_size);
        const uint32_t remaining_in_block = file_system->block_size - block_offset;
        const uint32_t remaining_in_file = requested - *bytes_read;
        const uint32_t amount = remaining_in_block < remaining_in_file ? remaining_in_block : remaining_in_file;
        for(uint32_t index = 0; index < amount; ++index) {
            destination[*bytes_read + index] =
                block_result == DataBlockResult::Hole ? 0 : g_io_buffer[block_offset + index];
        }
        *bytes_read += amount;
        position += amount;
    }
    synchronization::unlock(&file_system->io_lock, previous_state);
    return Status::Success;
}

} // namespace ext2

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-magic-numbers, bugprone-easily-swappable-parameters)
