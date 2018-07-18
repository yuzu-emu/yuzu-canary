// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "partition_filesystem.h"

namespace Loader {
enum class ResultStatus;
}

namespace FileSys {

enum class ProgramAddressSpaceType : u8 {
    Is64Bit = 1,
    Is32Bit = 2,
};

enum class ProgramFilePermission : u64 {
    MountContent = 1ULL << 0,
    SaveDataBackup = 1ULL << 5,
    SdCard = 1ULL << 21,
    Calibration = 1ULL << 34,
    Bit62 = 1ULL << 62,
    Everything = 1ULL << 63,
};

/**
 * Helper which implements an interface to parse Program Description Metadata (NPDM)
 * Data can either be loaded from a file path or with data and an offset into it.
 */
class ProgramMetadata {
public:
    Loader::ResultStatus Load(VirtualFile file);

    bool Is64BitProgram() const;
    ProgramAddressSpaceType GetAddressSpaceType() const;
    u8 GetMainThreadPriority() const;
    u8 GetMainThreadCore() const;
    u32 GetMainThreadStackSize() const;
    u64 GetTitleID() const;
    u64 GetFilesystemPermissions() const;

    void Print() const;

private:
    // TODO(DarkLordZach): BitField is not trivially copyable.
    struct Header {
        std::array<char, 4> magic;
        std::array<u8, 8> reserved;
        union {
            u8 flags;

            BitField<0, 1, u8> has_64_bit_instructions;
            BitField<1, 3, ProgramAddressSpaceType> address_space_type;
            BitField<4, 4, u8> reserved_2;
        };
        u8 reserved_3;
        u8 main_thread_priority;
        u8 main_thread_cpu;
        std::array<u8, 8> reserved_4;
        u32_le process_category;
        u32_le main_stack_size;
        std::array<u8, 0x10> application_name;
        std::array<u8, 0x40> reserved_5;
        u32_le aci_offset;
        u32_le aci_size;
        u32_le acid_offset;
        u32_le acid_size;
    };

    static_assert(sizeof(Header) == 0x80, "NPDM header structure size is wrong");

    // TODO(DarkLordZach): BitField is not trivially copyable.
    struct AcidHeader {
        std::array<u8, 0x100> signature;
        std::array<u8, 0x100> nca_modulus;
        std::array<char, 4> magic;
        u32_le nca_size;
        std::array<u8, 0x4> reserved;
        union {
            u32 flags;

            BitField<0, 1, u32> is_retail;
            BitField<1, 31, u32> flags_unk;
        };
        u64_le title_id_min;
        u64_le title_id_max;
        u32_le fac_offset;
        u32_le fac_size;
        u32_le sac_offset;
        u32_le sac_size;
        u32_le kac_offset;
        u32_le kac_size;
        INSERT_PADDING_BYTES(0x8);
    };

    static_assert(sizeof(AcidHeader) == 0x240, "ACID header structure size is wrong");

    struct AciHeader {
        std::array<char, 4> magic;
        std::array<u8, 0xC> reserved;
        u64_le title_id;
        INSERT_PADDING_BYTES(0x8);
        u32_le fah_offset;
        u32_le fah_size;
        u32_le sac_offset;
        u32_le sac_size;
        u32_le kac_offset;
        u32_le kac_size;
        INSERT_PADDING_BYTES(0x8);
    };

    static_assert(sizeof(AciHeader) == 0x40, "ACI0 header structure size is wrong");

#pragma pack(push, 1)

    struct FileAccessControl {
        u8 version;
        INSERT_PADDING_BYTES(3);
        u64_le permissions;
        std::array<u8, 0x20> unknown;
    };

    static_assert(sizeof(FileAccessControl) == 0x2C, "FS access control structure size is wrong");

    struct FileAccessHeader {
        u8 version;
        INSERT_PADDING_BYTES(3);
        u64_le permissions;
        u32_le unk_offset;
        u32_le unk_size;
        u32_le unk_offset_2;
        u32_le unk_size_2;
    };

    static_assert(sizeof(FileAccessHeader) == 0x1C, "FS access header structure size is wrong");

#pragma pack(pop)

    Header npdm_header;
    AciHeader aci_header;
    AcidHeader acid_header;

    FileAccessControl acid_file_access;
    FileAccessHeader aci_file_access;
};

} // namespace FileSys
