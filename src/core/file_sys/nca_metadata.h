// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <memory>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/vfs.h"

namespace FileSys {
class CNMT;

struct CNMTHeader;
struct OptionalHeader;

enum class TitleType : u8 {
    SystemProgram = 0x01,
    SystemDataArchive = 0x02,
    SystemUpdate = 0x03,
    FirmwarePackageA = 0x04,
    FirmwarePackageB = 0x05,
    Application = 0x80,
    Update = 0x81,
    AOC = 0x82,
    DeltaTitle = 0x83,
};

bool operator>=(TitleType lhs, TitleType rhs);
bool operator<=(TitleType lhs, TitleType rhs);

enum class ContentRecordType : u8 {
    Meta = 0,
    Program = 1,
    Data = 2,
    Control = 3,
    Manual = 4,
    Legal = 5,
    Patch = 6,
};

struct ContentRecord {
    std::array<u8, 0x20> hash;
    std::array<u8, 0x10> nca_id;
    std::array<u8, 0x6> size;
    ContentRecordType type;
    INSERT_PADDING_BYTES(1);
};
static_assert(sizeof(ContentRecord) == 0x38, "ContentRecord has incorrect size.");

constexpr ContentRecord EMPTY_META_CONTENT_RECORD{{}, {}, {}, ContentRecordType::Meta, {}};

struct MetaRecord {
    u64_le title_id;
    u32_le title_version;
    TitleType type;
    u8 install_byte;
    INSERT_PADDING_BYTES(2);
};
static_assert(sizeof(MetaRecord) == 0x10, "MetaRecord has incorrect size.");

struct OptionalHeader {
    u64_le title_id;
    u64_le minimum_version;
};
static_assert(sizeof(OptionalHeader) == 0x10, "OptionalHeader has incorrect size.");

struct CNMTHeader {
    u64_le title_id;
    u32_le title_version;
    TitleType type;
    INSERT_PADDING_BYTES(1);
    u16_le table_offset;
    u16_le number_content_entries;
    u16_le number_meta_entries;
    INSERT_PADDING_BYTES(12);
};
static_assert(sizeof(CNMTHeader) == 0x20, "CNMTHeader has incorrect size.");

// A class representing the format used by NCA metadata files, typically named {}.cnmt.nca or
// meta0.ncd. These describe which NCA's belong with which titles in the registered cache.
class CNMT {
public:
    explicit CNMT(VirtualFile file);
    CNMT(CNMTHeader header, OptionalHeader opt_header, std::vector<ContentRecord> content_records,
         std::vector<MetaRecord> meta_records);

    u64 GetTitleID() const;
    u32 GetTitleVersion() const;
    TitleType GetType() const;

    const std::vector<ContentRecord>& GetContentRecords() const;
    const std::vector<MetaRecord>& GetMetaRecords() const;

    bool UnionRecords(const CNMT& other);
    std::vector<u8> Serialize() const;

private:
    CNMTHeader header;
    OptionalHeader opt_header;
    std::vector<ContentRecord> content_records;
    std::vector<MetaRecord> meta_records;

    // TODO(DarkLordZach): According to switchbrew, for Patch-type there is additional data
    // after the table. This is not documented, unfortunately.
};

} // namespace FileSys
