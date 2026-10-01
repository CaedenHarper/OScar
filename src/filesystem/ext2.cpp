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
constexpr uint32_t kIndirectBlockCount = kInodeBlockPointerCount;

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

bool write_fs_block(const FileSystem* file_system, uint32_t block) {
    if(file_system == nullptr || block >= file_system->block_count) {
        return false;
    }
    const auto geometry = block_device::get_geometry(file_system->device);
    if(geometry.block_size == 0 || file_system->block_size % geometry.block_size != 0) {
        return false;
    }
    const uint32_t device_blocks = file_system->block_size / geometry.block_size;
    const uint64_t first_device_block = static_cast<uint64_t>(block) * device_blocks;
    return first_device_block <= UINT64_MAX - device_blocks &&
           block_device::write(file_system->device, first_device_block, device_blocks, g_io_buffer) ==
               block_device::Status::Success;
}

bool valid_inode_number(const FileSystem* file_system, uint32_t inode_number);

void write_u16(uint8_t* bytes, uint32_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void write_u32(uint8_t* bytes, uint32_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t descriptor_block(const FileSystem* file_system, uint32_t group) {
    return (file_system->block_size == kBlockSizeMinimum ? 2 : 1) + (group * 32U) / file_system->block_size;
}

uint32_t descriptor_offset(const FileSystem* file_system, uint32_t group) {
    return (group * 32U) % file_system->block_size;
}

bool group_descriptor(
    const FileSystem* file_system,
    uint32_t group,
    uint32_t* block_bitmap,
    uint32_t* inode_bitmap,
    uint16_t* free_blocks,
    uint16_t* free_inodes
) {
    if(file_system == nullptr || group >= file_system->group_count || block_bitmap == nullptr ||
       inode_bitmap == nullptr || free_blocks == nullptr || free_inodes == nullptr ||
       !read_fs_block(file_system, descriptor_block(file_system, group))) {
        return false;
    }
    const uint32_t offset = descriptor_offset(file_system, group);
    if(offset + 18 > file_system->block_size) {
        return false;
    }
    *block_bitmap = read_u32(g_io_buffer + offset, 0);
    *inode_bitmap = read_u32(g_io_buffer + offset, 4);
    *free_blocks = read_u16(g_io_buffer + offset, 12);
    *free_inodes = read_u16(g_io_buffer + offset, 14);
    return *block_bitmap < file_system->block_count && *inode_bitmap < file_system->block_count;
}

bool update_group_counts(const FileSystem* file_system, uint32_t group, int32_t block_delta, int32_t inode_delta) {
    if(file_system == nullptr || group >= file_system->group_count ||
       !read_fs_block(file_system, descriptor_block(file_system, group))) {
        return false;
    }
    const uint32_t offset = descriptor_offset(file_system, group);
    const int32_t free_blocks = static_cast<int32_t>(read_u16(g_io_buffer + offset, 12)) + block_delta;
    const int32_t free_inodes = static_cast<int32_t>(read_u16(g_io_buffer + offset, 14)) + inode_delta;
    if(offset + 16 > file_system->block_size || free_blocks < 0 || free_blocks > UINT16_MAX || free_inodes < 0 ||
       free_inodes > UINT16_MAX) {
        return false;
    }
    write_u16(g_io_buffer + offset, 12, static_cast<uint16_t>(free_blocks));
    write_u16(g_io_buffer + offset, 14, static_cast<uint16_t>(free_inodes));
    return write_fs_block(file_system, descriptor_block(file_system, group));
}

bool update_superblock_counts(const FileSystem* file_system, int32_t block_delta, int32_t inode_delta) {
    if(file_system == nullptr || !read_fs_block(file_system, kSuperblockOffset / file_system->block_size)) {
        return false;
    }
    const uint32_t offset = kSuperblockOffset % file_system->block_size;
    const int64_t free_blocks = static_cast<int64_t>(read_u32(g_io_buffer + offset, 12)) + block_delta;
    const int64_t free_inodes = static_cast<int64_t>(read_u32(g_io_buffer + offset, 16)) + inode_delta;
    if(free_blocks < 0 || free_blocks > UINT32_MAX || free_inodes < 0 || free_inodes > UINT32_MAX) {
        return false;
    }
    write_u32(g_io_buffer + offset, 12, static_cast<uint32_t>(free_blocks));
    write_u32(g_io_buffer + offset, 16, static_cast<uint32_t>(free_inodes));
    return write_fs_block(file_system, kSuperblockOffset / file_system->block_size);
}

bool allocate_bitmap_entry(const FileSystem* file_system, uint32_t bitmap_block, uint32_t limit, uint32_t* bit) {
    if(file_system == nullptr || bit == nullptr || limit == 0 || limit > file_system->block_size * 8U ||
       !read_fs_block(file_system, bitmap_block)) {
        return false;
    }
    for(uint32_t candidate = 0; candidate < limit; ++candidate) {
        const uint32_t byte = candidate / 8U;
        const uint8_t mask = static_cast<uint8_t>(1U << (candidate % 8U));
        if((g_io_buffer[byte] & mask) == 0) {
            g_io_buffer[byte] = static_cast<uint8_t>(g_io_buffer[byte] | mask);
            if(!write_fs_block(file_system, bitmap_block)) {
                return false;
            }
            *bit = candidate;
            return true;
        }
    }
    return false;
}

bool allocate_block(const FileSystem* file_system, uint32_t* block) {
    if(file_system == nullptr || block == nullptr) {
        return false;
    }
    for(uint32_t group = 0; group < file_system->group_count; ++group) {
        uint32_t block_bitmap = 0;
        uint32_t inode_bitmap = 0;
        uint16_t free_blocks = 0;
        uint16_t free_inodes = 0;
        if(!group_descriptor(file_system, group, &block_bitmap, &inode_bitmap, &free_blocks, &free_inodes) ||
           free_blocks == 0) {
            continue;
        }
        // For a 1 KiB ext2 filesystem, group zero begins at block one because
        // block zero is reserved for boot code. Bitmap bit zero therefore names
        // first_data_block, not physical block zero; omitting this offset would
        // overwrite the boot area and eventually make indirect pointers invalid.
        const uint64_t group_start = static_cast<uint64_t>(file_system->first_data_block) +
                                     static_cast<uint64_t>(group) * file_system->blocks_per_group;
        const uint64_t group_end = group + 1 == file_system->group_count ? file_system->block_count
                                                                         : group_start + file_system->blocks_per_group;
        const uint32_t limit = static_cast<uint32_t>(group_end - group_start);
        uint32_t bit = 0;
        if(!allocate_bitmap_entry(file_system, block_bitmap, limit, &bit)) {
            continue;
        }
        const uint64_t selected = group_start + bit;
        if(selected >= file_system->block_count || !update_group_counts(file_system, group, -1, 0) ||
           !update_superblock_counts(file_system, -1, 0)) {
            return false;
        }
        *block = static_cast<uint32_t>(selected);
        return true;
    }
    return false;
}

bool free_bitmap_entry(const FileSystem* file_system, uint32_t bitmap_block, uint32_t bit) {
    if(file_system == nullptr || bit >= file_system->block_size * 8U || !read_fs_block(file_system, bitmap_block)) {
        return false;
    }
    const uint32_t byte = bit / 8U;
    const uint8_t mask = static_cast<uint8_t>(1U << (bit % 8U));
    if((g_io_buffer[byte] & mask) == 0) {
        return false;
    }
    g_io_buffer[byte] = static_cast<uint8_t>(g_io_buffer[byte] & ~mask);
    return write_fs_block(file_system, bitmap_block);
}

bool free_block(const FileSystem* file_system, uint32_t block) {
    if(file_system == nullptr || block < file_system->first_data_block || block >= file_system->block_count) {
        return false;
    }
    const uint32_t relative = block - file_system->first_data_block;
    const uint32_t group = relative / file_system->blocks_per_group;
    const uint32_t bit = relative % file_system->blocks_per_group;
    uint32_t block_bitmap = 0;
    uint32_t inode_bitmap = 0;
    uint16_t free_blocks = 0;
    uint16_t free_inodes = 0;
    if(!group_descriptor(file_system, group, &block_bitmap, &inode_bitmap, &free_blocks, &free_inodes) ||
       !free_bitmap_entry(file_system, block_bitmap, bit) || !update_group_counts(file_system, group, 1, 0) ||
       !update_superblock_counts(file_system, 1, 0)) {
        return false;
    }
    return true;
}

bool allocate_inode(const FileSystem* file_system, uint32_t* inode_number) {
    if(file_system == nullptr || inode_number == nullptr) {
        return false;
    }
    for(uint32_t group = 0; group < file_system->group_count; ++group) {
        uint32_t block_bitmap = 0;
        uint32_t inode_bitmap = 0;
        uint16_t free_blocks = 0;
        uint16_t free_inodes = 0;
        if(!group_descriptor(file_system, group, &block_bitmap, &inode_bitmap, &free_blocks, &free_inodes) ||
           free_inodes == 0) {
            continue;
        }
        const uint64_t first_inode = static_cast<uint64_t>(group) * file_system->inodes_per_group + 1;
        const uint32_t remaining = first_inode > file_system->inode_count
                                       ? 0
                                       : static_cast<uint32_t>(file_system->inode_count - first_inode + 1);
        const uint32_t limit = remaining < file_system->inodes_per_group ? remaining : file_system->inodes_per_group;
        uint32_t bit = 0;
        if(limit == 0 || !allocate_bitmap_entry(file_system, inode_bitmap, limit, &bit) ||
           !update_group_counts(file_system, group, 0, -1) || !update_superblock_counts(file_system, 0, -1)) {
            continue;
        }
        *inode_number = static_cast<uint32_t>(first_inode + bit);
        return true;
    }
    return false;
}

bool free_inode_number(const FileSystem* file_system, uint32_t inode_number) {
    if(!valid_inode_number(file_system, inode_number)) {
        return false;
    }
    const uint32_t relative = inode_number - 1;
    const uint32_t group = relative / file_system->inodes_per_group;
    const uint32_t bit = relative % file_system->inodes_per_group;
    uint32_t block_bitmap = 0;
    uint32_t inode_bitmap = 0;
    uint16_t free_blocks = 0;
    uint16_t free_inodes = 0;
    if(!group_descriptor(file_system, group, &block_bitmap, &inode_bitmap, &free_blocks, &free_inodes) ||
       !free_bitmap_entry(file_system, inode_bitmap, bit) || !update_group_counts(file_system, group, 0, 1) ||
       !update_superblock_counts(file_system, 0, 1)) {
        return false;
    }
    return true;
}

bool inode_location(
    const FileSystem* file_system,
    uint32_t inode_number,
    uint32_t* table_block,
    uint32_t* table_offset
) {
    if(!valid_inode_number(file_system, inode_number) || table_block == nullptr || table_offset == nullptr) {
        return false;
    }
    const uint32_t group = (inode_number - 1) / file_system->inodes_per_group;
    const uint32_t index = (inode_number - 1) % file_system->inodes_per_group;
    uint32_t block_bitmap = 0;
    uint32_t inode_bitmap = 0;
    uint16_t free_blocks = 0;
    uint16_t free_inodes = 0;
    if(!group_descriptor(file_system, group, &block_bitmap, &inode_bitmap, &free_blocks, &free_inodes)) {
        return false;
    }
    const uint64_t inode_offset = static_cast<uint64_t>(index) * file_system->inode_size;
    if(inode_offset % file_system->block_size + file_system->inode_size > file_system->block_size) {
        return false;
    }
    // The inode table location is stored in the descriptor at offset 8.
    if(!read_fs_block(file_system, descriptor_block(file_system, group))) {
        return false;
    }
    const uint32_t table = read_u32(g_io_buffer + descriptor_offset(file_system, group), 8);
    *table_block = static_cast<uint32_t>(static_cast<uint64_t>(table) + inode_offset / file_system->block_size);
    *table_offset = static_cast<uint32_t>(inode_offset % file_system->block_size);
    return *table_block < file_system->block_count;
}

bool write_inode(const FileSystem* file_system, const Inode& inode) {
    uint32_t table_block = 0;
    uint32_t table_offset = 0;
    if(!inode_location(file_system, inode.number, &table_block, &table_offset) ||
       !read_fs_block(file_system, table_block)) {
        return false;
    }
    write_u16(g_io_buffer + table_offset, 0, inode.mode);
    write_u16(g_io_buffer + table_offset, 2, inode.uid);
    write_u32(g_io_buffer + table_offset, 4, static_cast<uint32_t>(inode.size));
    write_u16(g_io_buffer + table_offset, 24, inode.gid);
    write_u16(g_io_buffer + table_offset, 26, inode.links);
    write_u32(g_io_buffer + table_offset, 28, inode.sectors);
    for(uint32_t index = 0; index < kIndirectBlockCount; ++index) {
        write_u32(g_io_buffer + table_offset, 40 + index * sizeof(uint32_t), inode.blocks[index]);
    }
    write_u32(g_io_buffer + table_offset, 108, static_cast<uint32_t>(inode.size >> 32U));
    return write_fs_block(file_system, table_block);
}

bool clear_inode(const FileSystem* file_system, uint32_t inode_number) {
    uint32_t table_block = 0;
    uint32_t table_offset = 0;
    if(!inode_location(file_system, inode_number, &table_block, &table_offset) ||
       !read_fs_block(file_system, table_block)) {
        return false;
    }
    for(uint32_t index = 0; index < file_system->inode_size; ++index) {
        g_io_buffer[table_offset + index] = 0;
    }
    return write_fs_block(file_system, table_block);
}

bool zero_block(const FileSystem* file_system, uint32_t block) {
    for(uint32_t index = 0; index < file_system->block_size; ++index) {
        g_io_buffer[index] = 0;
    }
    return write_fs_block(file_system, block);
}

bool valid_inode_number(const FileSystem* file_system, uint32_t inode_number) {
    return file_system != nullptr && inode_number != 0 && inode_number <= file_system->inode_count;
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
    const uint64_t table_block = static_cast<uint64_t>(inode_table) + inode_offset / file_system->block_size;
    const uint32_t table_offset = static_cast<uint32_t>(inode_offset % file_system->block_size);
    if(table_block >= file_system->block_count || table_offset + file_system->inode_size > file_system->block_size ||
       !read_fs_block(file_system, static_cast<uint32_t>(table_block))) {
        return false;
    }

    const uint8_t* raw_inode = g_io_buffer + table_offset;
    inode->number = inode_number;
    inode->mode = read_u16(raw_inode, 0);
    inode->uid = read_u16(raw_inode, 2);
    inode->gid = read_u16(raw_inode, 24);
    inode->size = read_u32(raw_inode, 4);
    inode->links = read_u16(raw_inode, 26);
    inode->sectors = read_u32(raw_inode, 28);
    const uint16_t file_type = inode->mode & 0xf000U;
    if((file_type != kDirectoryType && file_type != 0x8000U) || inode->links == 0) {
        return false;
    }
    inode->directory = file_type == kDirectoryType;
    if(!inode->directory) {
        inode->size |= static_cast<uint64_t>(read_u32(raw_inode, 108)) << 32U;
    }
    if(direct_blocks != nullptr) {
        for(uint32_t index = 0; index < kIndirectBlockCount; ++index) {
            direct_blocks[index] = read_u32(raw_inode, 40 + index * sizeof(uint32_t));
            inode->blocks[index] = direct_blocks[index];
        }
    }
    const uint64_t maximum_blocks =
        kDirectBlockCount + file_system->block_size / sizeof(uint32_t) +
        (file_system->block_size / sizeof(uint32_t)) * (file_system->block_size / sizeof(uint32_t));
    if(inode->size > static_cast<uint64_t>(maximum_blocks) * file_system->block_size ||
       (inode->directory && inode->size % file_system->block_size != 0)) {
        return false;
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

bool allocate_data_block(
    const FileSystem* file_system,
    uint32_t* inode_blocks,
    uint64_t logical_block,
    uint32_t* sectors_added
) {
    if(file_system == nullptr || inode_blocks == nullptr || sectors_added == nullptr) {
        return false;
    }
    const uint64_t pointers_per_block = file_system->block_size / sizeof(uint32_t);
    uint32_t data_block = 0;
    if(logical_block < kDirectBlockCount) {
        if(!allocate_block(file_system, &data_block)) {
            return false;
        }
        inode_blocks[logical_block] = data_block;
        *sectors_added += file_system->block_size / 512U;
        return true;
    }

    logical_block -= kDirectBlockCount;
    if(logical_block < pointers_per_block) {
        if(inode_blocks[12] == 0) {
            if(!allocate_block(file_system, &inode_blocks[12]) || !zero_block(file_system, inode_blocks[12])) {
                return false;
            }
            *sectors_added += file_system->block_size / 512U;
        }
        if(!read_fs_block(file_system, inode_blocks[12])) {
            return false;
        }
        uint32_t* pointers = reinterpret_cast<uint32_t*>(g_io_buffer);
        if(pointers[logical_block] != 0) {
            return true;
        }
        if(!allocate_block(file_system, &data_block)) {
            return false;
        }
        if(!read_fs_block(file_system, inode_blocks[12])) {
            return false;
        }
        pointers = reinterpret_cast<uint32_t*>(g_io_buffer);
        pointers[logical_block] = data_block;
        if(!write_fs_block(file_system, inode_blocks[12])) {
            return false;
        }
        *sectors_added += file_system->block_size / 512U;
        return true;
    }

    logical_block -= pointers_per_block;
    if(logical_block >= pointers_per_block * pointers_per_block) {
        return false;
    }
    if(inode_blocks[13] == 0) {
        if(!allocate_block(file_system, &inode_blocks[13]) || !zero_block(file_system, inode_blocks[13])) {
            return false;
        }
        *sectors_added += file_system->block_size / 512U;
    }
    const uint32_t first_index = static_cast<uint32_t>(logical_block / pointers_per_block);
    const uint32_t second_index = static_cast<uint32_t>(logical_block % pointers_per_block);
    if(!read_fs_block(file_system, inode_blocks[13])) {
        return false;
    }
    uint32_t indirect_block = reinterpret_cast<uint32_t*>(g_io_buffer)[first_index];
    if(indirect_block == 0) {
        if(!allocate_block(file_system, &indirect_block) || !zero_block(file_system, indirect_block)) {
            return false;
        }
        *sectors_added += file_system->block_size / 512U;
        if(!read_fs_block(file_system, inode_blocks[13])) {
            return false;
        }
        reinterpret_cast<uint32_t*>(g_io_buffer)[first_index] = indirect_block;
        if(!write_fs_block(file_system, inode_blocks[13])) {
            return false;
        }
    }
    if(!read_fs_block(file_system, indirect_block)) {
        return false;
    }
    uint32_t* pointers = reinterpret_cast<uint32_t*>(g_io_buffer);
    if(pointers[second_index] != 0) {
        return true;
    }
    if(!allocate_block(file_system, &data_block)) {
        return false;
    }
    if(!read_fs_block(file_system, indirect_block)) {
        return false;
    }
    pointers = reinterpret_cast<uint32_t*>(g_io_buffer);
    pointers[second_index] = data_block;
    if(!write_fs_block(file_system, indirect_block)) {
        return false;
    }
    *sectors_added += file_system->block_size / 512U;
    return true;
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
    const uint64_t block_count =
        directory.size / file_system->block_size + (directory.size % file_system->block_size != 0 ? 1 : 0);
    for(uint64_t block_index = 0; block_index < block_count; ++block_index) {
        uint32_t physical_block = 0;
        const DataBlockResult block_result = get_data_block(file_system, inode_blocks, block_index, &physical_block);
        if(block_result == DataBlockResult::Unsupported) {
            return Status::Unsupported;
        }
        if(block_result == DataBlockResult::Corrupt) {
            return Status::Corrupt;
        }
        if(block_result != DataBlockResult::Success || !read_fs_block(file_system, physical_block)) {
            return block_result == DataBlockResult::IoError ? Status::IoError : Status::Corrupt;
        }
        uint32_t offset = 0;
        const uint64_t remaining = directory.size - block_index * file_system->block_size;
        const uint32_t available =
            remaining < file_system->block_size ? static_cast<uint32_t>(remaining) : file_system->block_size;
        while(offset + kDirectoryEntryHeaderSize <= available) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 || entry_size > available - offset ||
               name_length > 255 || (entry_inode != 0 && name_length == 0) ||
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

struct DirectoryEntryLocation {
    uint32_t block;
    uint32_t offset;
    uint16_t size;
    uint32_t inode;
};

uint32_t directory_block_count(const FileSystem* file_system, const Inode& directory) {
    return static_cast<uint32_t>(
        directory.size / file_system->block_size + (directory.size % file_system->block_size != 0 ? 1 : 0)
    );
}

bool valid_name(const char* name, uint32_t* length) {
    if(name == nullptr || length == nullptr || *name == '\0' || (name[0] == '.' && name[1] == '\0') ||
       (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return false;
    }
    uint32_t count = 0;
    while(name[count] != '\0') {
        if(name[count] == '/' || count == UINT8_MAX) {
            return false;
        }
        ++count;
    }
    *length = count;
    return true;
}

bool read_directory_block(
    const FileSystem* file_system,
    const Inode& directory,
    uint32_t logical_block,
    uint32_t* physical_block
) {
    uint32_t blocks[kIndirectBlockCount] = {};
    Inode verified = {};
    if(!read_inode(file_system, directory.number, &verified, blocks)) {
        return false;
    }
    const DataBlockResult result = get_data_block(file_system, blocks, logical_block, physical_block);
    return result == DataBlockResult::Success && read_fs_block(file_system, *physical_block);
}

bool find_directory_entry(
    const FileSystem* file_system,
    const Inode& directory,
    const char* name,
    uint32_t name_length,
    DirectoryEntryLocation* location
) {
    const uint32_t block_count = directory_block_count(file_system, directory);
    for(uint32_t logical_block = 0; logical_block < block_count; ++logical_block) {
        uint32_t physical_block = 0;
        if(!read_directory_block(file_system, directory, logical_block, &physical_block)) {
            return false;
        }
        uint32_t offset = 0;
        while(offset < file_system->block_size) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t entry_name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 ||
               entry_size > file_system->block_size - offset || (entry_inode != 0 && entry_name_length == 0) ||
               entry_name_length > entry_size - kDirectoryEntryHeaderSize) {
                return false;
            }
            if(entry_inode != 0 &&
               component_equals(
                   g_io_buffer + offset + kDirectoryEntryHeaderSize, entry_name_length, name, name_length
               )) {
                *location = {physical_block, offset, entry_size, entry_inode};
                return true;
            }
            offset += entry_size;
        }
    }
    return false;
}

bool directory_empty(const FileSystem* file_system, const Inode& directory) {
    const uint32_t block_count = directory_block_count(file_system, directory);
    for(uint32_t logical_block = 0; logical_block < block_count; ++logical_block) {
        uint32_t physical_block = 0;
        if(!read_directory_block(file_system, directory, logical_block, &physical_block)) {
            return false;
        }
        uint32_t offset = 0;
        while(offset < file_system->block_size) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t entry_name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 ||
               entry_size > file_system->block_size - offset || (entry_inode != 0 && entry_name_length == 0) ||
               entry_name_length > entry_size - kDirectoryEntryHeaderSize) {
                return false;
            }
            const uint8_t* entry_name = g_io_buffer + offset + kDirectoryEntryHeaderSize;
            const bool special = (entry_name_length == 1 && entry_name[0] == '.') ||
                                 (entry_name_length == 2 && entry_name[0] == '.' && entry_name[1] == '.');
            if(entry_inode != 0 && !special) {
                return false;
            }
            offset += entry_size;
        }
    }
    return true;
}

void write_directory_entry(
    uint32_t offset,
    uint32_t inode,
    uint16_t size,
    const char* name,
    uint32_t name_length,
    uint8_t type
) {
    write_u32(g_io_buffer + offset, 0, inode);
    write_u16(g_io_buffer + offset, 4, size);
    g_io_buffer[offset + 6] = static_cast<uint8_t>(name_length);
    g_io_buffer[offset + 7] = type;
    for(uint32_t index = 0; index < name_length; ++index) {
        g_io_buffer[offset + kDirectoryEntryHeaderSize + index] = static_cast<uint8_t>(name[index]);
    }
}

bool insert_directory_entry(
    const FileSystem* file_system,
    Inode* directory,
    uint32_t child_inode,
    const char* name,
    uint32_t name_length,
    uint8_t type
) {
    const uint16_t required = static_cast<uint16_t>((kDirectoryEntryHeaderSize + name_length + 3U) & ~3U);
    const uint32_t block_count = directory_block_count(file_system, *directory);
    for(uint32_t logical_block = 0; logical_block < block_count; ++logical_block) {
        uint32_t physical_block = 0;
        if(!read_directory_block(file_system, *directory, logical_block, &physical_block)) {
            return false;
        }
        uint32_t offset = 0;
        while(offset < file_system->block_size) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t entry_name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 ||
               entry_size > file_system->block_size - offset || (entry_inode != 0 && entry_name_length == 0) ||
               entry_name_length > entry_size - kDirectoryEntryHeaderSize) {
                return false;
            }
            if(entry_inode == 0 && entry_size >= required) {
                write_directory_entry(offset, child_inode, entry_size, name, name_length, type);
                return write_fs_block(file_system, physical_block);
            }
            const uint16_t used = static_cast<uint16_t>((kDirectoryEntryHeaderSize + entry_name_length + 3U) & ~3U);
            if(entry_inode != 0 && entry_size >= used + required) {
                write_u16(g_io_buffer + offset, 4, used);
                write_directory_entry(offset + used, child_inode, entry_size - used, name, name_length, type);
                return write_fs_block(file_system, physical_block);
            }
            offset += entry_size;
        }
    }

    if(directory->size % file_system->block_size != 0) {
        return false;
    }
    uint32_t* blocks = directory->blocks;
    const uint32_t logical_block = directory_block_count(file_system, *directory);
    uint32_t sectors_added = 0;
    if(!allocate_data_block(file_system, blocks, logical_block, &sectors_added)) {
        return false;
    }
    uint32_t physical_block = 0;
    if(get_data_block(file_system, blocks, logical_block, &physical_block) != DataBlockResult::Success ||
       !zero_block(file_system, physical_block)) {
        return false;
    }
    write_directory_entry(0, child_inode, static_cast<uint16_t>(file_system->block_size), name, name_length, type);
    if(!write_fs_block(file_system, physical_block)) {
        return false;
    }
    directory->size += file_system->block_size;
    directory->sectors += sectors_added;
    return true;
}

bool free_inode_blocks(const FileSystem* file_system, const Inode& inode) {
    const uint32_t pointer_count = file_system->block_size / sizeof(uint32_t);
    uint32_t pointers[kBlockSizeMaximum / sizeof(uint32_t)];
    for(uint32_t index = 0; index < kDirectBlockCount; ++index) {
        if(inode.blocks[index] != 0 && !free_block(file_system, inode.blocks[index])) {
            return false;
        }
    }
    if(inode.blocks[12] != 0) {
        if(!read_fs_block(file_system, inode.blocks[12])) {
            return false;
        }
        for(uint32_t index = 0; index < pointer_count; ++index) {
            pointers[index] = read_u32(g_io_buffer, index * sizeof(uint32_t));
        }
        for(uint32_t index = 0; index < pointer_count; ++index) {
            if(pointers[index] != 0 && !free_block(file_system, pointers[index])) {
                return false;
            }
        }
        if(!free_block(file_system, inode.blocks[12])) {
            return false;
        }
    }
    if(inode.blocks[13] != 0) {
        if(!read_fs_block(file_system, inode.blocks[13])) {
            return false;
        }
        for(uint32_t index = 0; index < pointer_count; ++index) {
            pointers[index] = read_u32(g_io_buffer, index * sizeof(uint32_t));
        }
        for(uint32_t index = 0; index < pointer_count; ++index) {
            if(pointers[index] == 0) {
                continue;
            }
            if(!read_fs_block(file_system, pointers[index])) {
                return false;
            }
            for(uint32_t second = 0; second < pointer_count; ++second) {
                const uint32_t data_block = read_u32(g_io_buffer, second * sizeof(uint32_t));
                if(data_block != 0 && !free_block(file_system, data_block)) {
                    return false;
                }
            }
            if(!free_block(file_system, pointers[index])) {
                return false;
            }
        }
        if(!free_block(file_system, inode.blocks[13])) {
            return false;
        }
    }
    return true;
}

bool destroy_inode(const FileSystem* file_system, const Inode& inode) {
    return free_inode_blocks(file_system, inode) && clear_inode(file_system, inode.number) &&
           free_inode_number(file_system, inode.number);
}

Status create_entry_locked(
    const FileSystem* file_system,
    const Inode& parent,
    const char* name,
    bool directory,
    Inode* created
) {
    uint32_t name_length = 0;
    Inode current_parent = {};
    if(!read_inode(file_system, parent.number, &current_parent, current_parent.blocks) || !current_parent.directory ||
       !valid_name(name, &name_length) || name_length > UINT8_MAX) {
        return Status::InvalidArgument;
    }
    Inode existing = {};
    const Status lookup_status = find_child(file_system, current_parent, name, name_length, &existing);
    if(lookup_status == Status::Success) {
        return Status::Exists;
    }
    if(lookup_status != Status::NotFound) {
        return lookup_status;
    }
    uint32_t inode_number = 0;
    if(!allocate_inode(file_system, &inode_number)) {
        return Status::NoSpace;
    }
    Inode child = {};
    child.number = inode_number;
    child.mode = static_cast<uint16_t>((directory ? kDirectoryType : 0x8000U) | (directory ? 0755U : 0644U));
    child.uid = current_parent.uid;
    child.gid = current_parent.gid;
    child.links = directory ? 2 : 1;
    if(directory) {
        uint32_t sectors_added = 0;
        if(!allocate_data_block(file_system, child.blocks, 0, &sectors_added)) {
            free_inode_number(file_system, inode_number);
            return Status::NoSpace;
        }
        child.size = file_system->block_size;
        child.sectors = sectors_added;
        uint32_t physical_block = 0;
        if(get_data_block(file_system, child.blocks, 0, &physical_block) != DataBlockResult::Success ||
           !zero_block(file_system, physical_block)) {
            free_inode_blocks(file_system, child);
            free_inode_number(file_system, inode_number);
            return Status::IoError;
        }
        write_directory_entry(0, child.number, 12, ".", 1, 2);
        write_directory_entry(
            12, current_parent.number, static_cast<uint16_t>(file_system->block_size - 12), "..", 2, 2
        );
        if(!write_fs_block(file_system, physical_block)) {
            free_inode_blocks(file_system, child);
            free_inode_number(file_system, inode_number);
            return Status::IoError;
        }
    }
    if(!write_inode(file_system, child)) {
        destroy_inode(file_system, child);
        return Status::IoError;
    }
    Inode updated_parent = current_parent;
    if(!insert_directory_entry(file_system, &updated_parent, child.number, name, name_length, directory ? 2 : 1)) {
        destroy_inode(file_system, child);
        return Status::NoSpace;
    }
    if(directory) {
        ++updated_parent.links;
    }
    if(!write_inode(file_system, updated_parent)) {
        return Status::IoError;
    }
    if(created != nullptr) {
        *created = child;
    }
    return Status::Success;
}

Status remove_entry_locked(const FileSystem* file_system, const Inode& parent, const char* name, bool directory) {
    uint32_t name_length = 0;
    Inode current_parent = {};
    if(!read_inode(file_system, parent.number, &current_parent, current_parent.blocks) || !current_parent.directory ||
       !valid_name(name, &name_length)) {
        return Status::InvalidArgument;
    }
    DirectoryEntryLocation location = {};
    if(!find_directory_entry(file_system, current_parent, name, name_length, &location)) {
        return Status::NotFound;
    }
    Inode child = {};
    if(!read_inode(file_system, location.inode, &child)) {
        return Status::Corrupt;
    }
    if(child.directory != directory) {
        return directory ? Status::NotDirectory : Status::IsDirectory;
    }
    if(directory && !directory_empty(file_system, child)) {
        return Status::NotEmpty;
    }
    if((directory && child.links != 2) || (!directory && child.links != 1)) {
        return Status::Unsupported;
    }
    if(!read_fs_block(file_system, location.block)) {
        return Status::IoError;
    }
    write_u32(g_io_buffer + location.offset, 0, 0);
    if(!write_fs_block(file_system, location.block)) {
        return Status::IoError;
    }
    Inode updated_parent = current_parent;
    if(directory && updated_parent.links > 0) {
        --updated_parent.links;
    }
    if(!write_inode(file_system, updated_parent) || !destroy_inode(file_system, child)) {
        return Status::IoError;
    }
    return Status::Success;
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
    const uint64_t device_bytes = static_cast<uint64_t>(device->geometry.block_count) * device->geometry.block_size;
    if(block_size < kBlockSizeMinimum || block_size > kBlockSizeMaximum ||
       block_size % device->geometry.block_size != 0 || blocks_count == 0 ||
       static_cast<uint64_t>(blocks_count) > device_bytes / block_size || blocks_per_group == 0 ||
       blocks_per_group > block_size * 8U || inodes_per_group == 0 || inodes_per_group > block_size * 8U ||
       inode_count == 0 || inode_size < 128 || inode_size > block_size || block_size % inode_size != 0 ||
       (incompatible & ~kDirectoryEntryFileTypeFeature) != 0) {
        return false;
    }
    if(first_data_block >= blocks_count || first_data_block != (block_size == kBlockSizeMinimum ? 1U : 0U)) {
        return false;
    }
    const uint64_t group_count =
        (static_cast<uint64_t>(blocks_count) - first_data_block + blocks_per_group - 1) / blocks_per_group;
    const uint64_t inode_table_blocks =
        (static_cast<uint64_t>(inodes_per_group) * inode_size + block_size - 1) / block_size;
    const uint64_t inode_groups = (static_cast<uint64_t>(inode_count) + inodes_per_group - 1) / inodes_per_group;
    if(group_count == 0 || group_count > kMaximumGroups || inode_groups > group_count || inode_table_blocks == 0 ||
       inode_table_blocks > UINT32_MAX) {
        return false;
    }
    file_system->device = device;
    file_system->block_count = blocks_count;
    file_system->block_size = block_size;
    file_system->first_data_block = first_data_block;
    file_system->blocks_per_group = blocks_per_group;
    file_system->inode_count = inode_count;
    file_system->inodes_per_group = inodes_per_group;
    file_system->inode_size = inode_size;
    file_system->group_count = static_cast<uint32_t>(group_count);
    file_system->inode_table_blocks = static_cast<uint32_t>(inode_table_blocks);
    file_system->mounted = true;
    synchronization::initialize(&file_system->io_lock);

    // Validate every group descriptor while mounting. Deferring this check would let
    // a corrupt bitmap or inode table redirect later writes into arbitrary blocks.
    for(uint32_t group = 0; group < file_system->group_count; ++group) {
        const uint32_t descriptor = descriptor_block(file_system, group);
        if(descriptor >= file_system->block_count || !read_fs_block(file_system, descriptor)) {
            *file_system = {};
            return false;
        }
        const uint32_t offset = descriptor_offset(file_system, group);
        if(offset + 32 > file_system->block_size) {
            *file_system = {};
            return false;
        }
        const uint32_t block_bitmap = read_u32(g_io_buffer + offset, 0);
        const uint32_t inode_bitmap = read_u32(g_io_buffer + offset, 4);
        const uint32_t inode_table = read_u32(g_io_buffer + offset, 8);
        const uint16_t free_blocks = read_u16(g_io_buffer + offset, 12);
        const uint16_t free_inodes = read_u16(g_io_buffer + offset, 14);
        const uint64_t group_first_block =
            static_cast<uint64_t>(first_data_block) + static_cast<uint64_t>(group) * blocks_per_group;
        const uint32_t group_blocks =
            group_first_block >= blocks_count
                ? 0
                : static_cast<uint32_t>(
                      (blocks_count - group_first_block) < blocks_per_group ? blocks_count - group_first_block
                                                                            : blocks_per_group
                  );
        const uint64_t group_first_inode = static_cast<uint64_t>(group) * inodes_per_group;
        const uint32_t group_inodes =
            group_first_inode >= inode_count
                ? 0
                : static_cast<uint32_t>(
                      (inode_count - group_first_inode) < inodes_per_group ? inode_count - group_first_inode
                                                                           : inodes_per_group
                  );
        if(block_bitmap >= file_system->block_count || inode_bitmap >= file_system->block_count ||
           inode_table >= file_system->block_count ||
           static_cast<uint64_t>(inode_table) + file_system->inode_table_blocks > file_system->block_count ||
           free_blocks > group_blocks || free_inodes > group_inodes || group_blocks == 0 || group_inodes == 0 ||
           block_bitmap < group_first_block || block_bitmap >= group_first_block + group_blocks ||
           inode_bitmap < group_first_block || inode_bitmap >= group_first_block + group_blocks ||
           inode_table < group_first_block ||
           inode_table + file_system->inode_table_blocks > group_first_block + group_blocks) {
            *file_system = {};
            return false;
        }
    }

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

Status read_directory(
    const FileSystem* file_system,
    const Inode* directory,
    uint32_t index,
    char* name,
    uint32_t name_capacity,
    Inode* inode
) {
    if(file_system == nullptr || !file_system->mounted || directory == nullptr || !directory->directory ||
       name == nullptr || name_capacity < 256 || inode == nullptr) {
        return Status::InvalidArgument;
    }

    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    Inode current = {};
    uint32_t blocks[kIndirectBlockCount] = {};
    if(!read_inode(file_system, directory->number, &current, blocks) || !current.directory) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Corrupt;
    }

    uint32_t remaining_index = index;
    const uint32_t block_count = directory_block_count(file_system, current);
    for(uint32_t block_index = 0; block_index < block_count; ++block_index) {
        uint32_t physical_block = 0;
        const DataBlockResult block_result = get_data_block(file_system, blocks, block_index, &physical_block);
        if(block_result == DataBlockResult::Unsupported) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Unsupported;
        }
        if(block_result == DataBlockResult::Corrupt ||
           (block_result == DataBlockResult::Success && !read_fs_block(file_system, physical_block))) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Corrupt;
        }
        if(block_result == DataBlockResult::IoError) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::IoError;
        }
        const uint64_t offset_in_directory = static_cast<uint64_t>(block_index) * file_system->block_size;
        const uint32_t available = current.size - offset_in_directory < file_system->block_size
                                       ? static_cast<uint32_t>(current.size - offset_in_directory)
                                       : file_system->block_size;
        uint32_t offset = 0;
        while(offset + kDirectoryEntryHeaderSize <= available) {
            const uint32_t entry_inode = read_u32(g_io_buffer + offset, 0);
            const uint16_t entry_size = read_u16(g_io_buffer + offset, 4);
            const uint8_t entry_name_length = g_io_buffer[offset + 6];
            if(entry_size < kDirectoryEntryHeaderSize || entry_size % 4 != 0 || entry_size > available - offset ||
               (entry_inode != 0 && entry_name_length == 0) ||
               entry_name_length > entry_size - kDirectoryEntryHeaderSize) {
                synchronization::unlock(&file_system->io_lock, previous_state);
                return Status::Corrupt;
            }
            const uint8_t* entry_name = g_io_buffer + offset + kDirectoryEntryHeaderSize;
            const bool is_dot = entry_name_length == 1 && entry_name[0] == '.';
            const bool is_dot_dot = entry_name_length == 2 && entry_name[0] == '.' && entry_name[1] == '.';
            if(entry_inode != 0 && !is_dot && !is_dot_dot) {
                if(remaining_index == 0) {
                    for(uint32_t character = 0; character < entry_name_length; ++character) {
                        name[character] = static_cast<char>(entry_name[character]);
                    }
                    name[entry_name_length] = '\0';
                    if(!read_inode(file_system, entry_inode, inode)) {
                        synchronization::unlock(&file_system->io_lock, previous_state);
                        return Status::Corrupt;
                    }
                    synchronization::unlock(&file_system->io_lock, previous_state);
                    return Status::Success;
                }
                --remaining_index;
            }
            offset += entry_size;
        }
        if(offset != available && available != 0) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Corrupt;
        }
    }
    synchronization::unlock(&file_system->io_lock, previous_state);
    return Status::NotFound;
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
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    uint32_t inode_blocks[kIndirectBlockCount] = {};
    Inode verified = {};
    if(!read_inode(file_system, inode->number, &verified, inode_blocks)) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Corrupt;
    }
    if(verified.directory) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::NotDirectory;
    }
    if(offset >= verified.size || length == 0) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Success;
    }
    const uint64_t available = verified.size - offset;
    const uint32_t requested = available < length ? static_cast<uint32_t>(available) : length;

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

Status write_file(
    const FileSystem* file_system,
    const Inode* inode,
    uint64_t offset,
    uint32_t length,
    const void* buffer,
    uint32_t* bytes_written
) {
    if(file_system == nullptr || !file_system->mounted || inode == nullptr || bytes_written == nullptr ||
       (length != 0 && buffer == nullptr)) {
        return Status::InvalidArgument;
    }
    *bytes_written = 0;
    if(length == 0) {
        return Status::Success;
    }
    if(offset > UINT64_MAX - length) {
        return Status::Unsupported;
    }

    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    Inode current = {};
    if(!read_inode(file_system, inode->number, &current, current.blocks)) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Corrupt;
    }
    if(current.directory) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::NotDirectory;
    }
    const uint64_t end = offset + length;
    const uint64_t maximum_blocks =
        12ULL + file_system->block_size / sizeof(uint32_t) +
        (file_system->block_size / sizeof(uint32_t)) * (file_system->block_size / sizeof(uint32_t));
    if((end + file_system->block_size - 1) / file_system->block_size > maximum_blocks) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::Unsupported;
    }

    const uint8_t* source = static_cast<const uint8_t*>(buffer);
    uint64_t position = offset;
    uint32_t sectors_added = 0;
    while(*bytes_written < length) {
        const uint64_t logical_block = position / file_system->block_size;
        uint32_t physical_block = 0;
        const DataBlockResult existing = get_data_block(file_system, current.blocks, logical_block, &physical_block);
        if(existing == DataBlockResult::Unsupported) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::Unsupported;
        }
        if(existing == DataBlockResult::Corrupt || existing == DataBlockResult::IoError) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return existing == DataBlockResult::Corrupt ? Status::Corrupt : Status::IoError;
        }
        if(existing == DataBlockResult::Hole) {
            if(!allocate_data_block(file_system, current.blocks, logical_block, &sectors_added)) {
                synchronization::unlock(&file_system->io_lock, previous_state);
                return Status::IoError;
            }
            physical_block = 0;
            const DataBlockResult allocated =
                get_data_block(file_system, current.blocks, logical_block, &physical_block);
            if(allocated != DataBlockResult::Success) {
                synchronization::unlock(&file_system->io_lock, previous_state);
                return Status::Corrupt;
            }
            if(!zero_block(file_system, physical_block)) {
                synchronization::unlock(&file_system->io_lock, previous_state);
                return Status::IoError;
            }
        } else if(!read_fs_block(file_system, physical_block)) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::IoError;
        }
        const uint32_t block_offset = static_cast<uint32_t>(position % file_system->block_size);
        const uint32_t amount = (file_system->block_size - block_offset) < (length - *bytes_written)
                                    ? file_system->block_size - block_offset
                                    : length - *bytes_written;
        for(uint32_t index = 0; index < amount; ++index) {
            g_io_buffer[block_offset + index] = source[*bytes_written + index];
        }
        if(!write_fs_block(file_system, physical_block)) {
            synchronization::unlock(&file_system->io_lock, previous_state);
            return Status::IoError;
        }
        *bytes_written += amount;
        position += amount;
    }
    if(end > current.size) {
        current.size = end;
    }
    current.sectors += sectors_added;
    if(!write_inode(file_system, current)) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::IoError;
    }
    const block_device::Status flush_status = block_device::flush(file_system->device);
    if(flush_status != block_device::Status::Success && flush_status != block_device::Status::Unsupported) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::IoError;
    }
    synchronization::unlock(&file_system->io_lock, previous_state);
    return Status::Success;
}

Status create(const FileSystem* file_system, const Inode* parent, const char* name, bool directory, Inode* inode) {
    if(file_system == nullptr || !file_system->mounted || parent == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const Status result = create_entry_locked(file_system, *parent, name, directory, inode);
    const block_device::Status flush_status = block_device::flush(file_system->device);
    synchronization::unlock(&file_system->io_lock, previous_state);
    if(result == Status::Success && flush_status != block_device::Status::Success &&
       flush_status != block_device::Status::Unsupported) {
        return Status::IoError;
    }
    return result;
}

Status unlink(const FileSystem* file_system, const Inode* parent, const char* name) {
    if(file_system == nullptr || !file_system->mounted || parent == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const Status result = remove_entry_locked(file_system, *parent, name, false);
    const block_device::Status flush_status = block_device::flush(file_system->device);
    synchronization::unlock(&file_system->io_lock, previous_state);
    if(result == Status::Success && flush_status != block_device::Status::Success &&
       flush_status != block_device::Status::Unsupported) {
        return Status::IoError;
    }
    return result;
}

Status remove_directory(const FileSystem* file_system, const Inode* parent, const char* name) {
    if(file_system == nullptr || !file_system->mounted || parent == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    const Status result = remove_entry_locked(file_system, *parent, name, true);
    const block_device::Status flush_status = block_device::flush(file_system->device);
    synchronization::unlock(&file_system->io_lock, previous_state);
    if(result == Status::Success && flush_status != block_device::Status::Success &&
       flush_status != block_device::Status::Unsupported) {
        return Status::IoError;
    }
    return result;
}

Status statistics(const FileSystem* file_system, Statistics* output) {
    if(file_system == nullptr || !file_system->mounted || output == nullptr) {
        return Status::InvalidArgument;
    }
    const interrupts::State previous_state = synchronization::lock(&file_system->io_lock);
    if(!read_fs_block(file_system, kSuperblockOffset / file_system->block_size)) {
        synchronization::unlock(&file_system->io_lock, previous_state);
        return Status::IoError;
    }
    const uint32_t offset = kSuperblockOffset % file_system->block_size;
    output->block_size = file_system->block_size;
    output->total_blocks = read_u32(g_io_buffer + offset, 4);
    output->free_blocks = read_u32(g_io_buffer + offset, 12);
    output->total_inodes = read_u32(g_io_buffer + offset, 0);
    output->free_inodes = read_u32(g_io_buffer + offset, 16);
    synchronization::unlock(&file_system->io_lock, previous_state);
    return output->free_blocks <= output->total_blocks && output->free_inodes <= output->total_inodes ? Status::Success
                                                                                                      : Status::Corrupt;
}

} // namespace ext2

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index,
//           readability-magic-numbers, bugprone-easily-swappable-parameters)
