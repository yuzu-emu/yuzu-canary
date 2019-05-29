// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/uuid.h"

namespace Service::Mii {

constexpr std::size_t MAX_MIIS = 100;
constexpr u32 INVALID_INDEX = 0xFFFFFFFF;

struct RandomParameters {
    u32 unknown_1;
    u32 unknown_2;
    u32 unknown_3;
};
static_assert(sizeof(RandomParameters) == 0xC, "RandomParameters has incorrect size.");

enum class Source : u32 {
    Database = 0,
    Default = 1,
    Account = 2,
    Friend = 3,
};

std::ostream& operator<<(std::ostream& os, Source source);

struct MiiInfo {
    Common::UUID uuid;
    std::array<char16_t, 11> name;
    u8 font_region;
    u8 favorite_color;
    u8 gender;
    u8 height;
    u8 weight;
    u8 mii_type;
    u8 mii_region;
    u8 face_type;
    u8 face_color;
    u8 face_wrinkle;
    u8 face_makeup;
    u8 hair_type;
    u8 hair_color;
    bool hair_flip;
    u8 eye_type;
    u8 eye_color;
    u8 eye_scale;
    u8 eye_aspect_ratio;
    u8 eye_rotate;
    u8 eye_x;
    u8 eye_y;
    u8 eyebrow_type;
    u8 eyebrow_color;
    u8 eyebrow_scale;
    u8 eyebrow_aspect_ratio;
    u8 eyebrow_rotate;
    u8 eyebrow_x;
    u8 eyebrow_y;
    u8 nose_type;
    u8 nose_scale;
    u8 nose_y;
    u8 mouth_type;
    u8 mouth_color;
    u8 mouth_scale;
    u8 mouth_aspect_ratio;
    u8 mouth_y;
    u8 facial_hair_color;
    u8 beard_type;
    u8 mustache_type;
    u8 mustache_scale;
    u8 mustache_y;
    u8 glasses_type;
    u8 glasses_color;
    u8 glasses_scale;
    u8 glasses_y;
    u8 mole_type;
    u8 mole_scale;
    u8 mole_x;
    u8 mole_y;
    INSERT_PADDING_BYTES(1);

    std::u16string Name() const;
};
static_assert(sizeof(MiiInfo) == 0x58, "MiiInfo has incorrect size.");
static_assert(std::has_unique_object_representations_v<MiiInfo>,
              "All bits of MiiInfo must contribute to its value.");

bool operator==(const MiiInfo& lhs, const MiiInfo& rhs);
bool operator!=(const MiiInfo& lhs, const MiiInfo& rhs);

#pragma pack(push, 4)
struct MiiInfoElement {
    MiiInfo info;
    Source source;
};
static_assert(sizeof(MiiInfoElement) == 0x5C, "MiiInfoElement has incorrect size.");

struct MiiStoreBitFields {
    union {
        u32 word_0;

        BitField<24, 8, u32> hair_type;
        BitField<23, 1, u32> mole_type;
        BitField<16, 7, u32> height;
        BitField<15, 1, u32> hair_flip;
        BitField<8, 7, u32> weight;
        BitField<0, 7, u32> hair_color;
    };

    union {
        u32 word_1;

        BitField<31, 1, u32> gender;
        BitField<24, 7, u32> eye_color;
        BitField<16, 7, u32> eyebrow_color;
        BitField<8, 7, u32> mouth_color;
        BitField<0, 7, u32> facial_hair_color;
    };

    union {
        u32 word_2;

        BitField<31, 1, u32> mii_type;
        BitField<24, 7, u32> glasses_color;
        BitField<22, 2, u32> font_region;
        BitField<16, 6, u32> eye_type;
        BitField<14, 2, u32> mii_region;
        BitField<8, 6, u32> mouth_type;
        BitField<5, 3, u32> glasses_scale;
        BitField<0, 5, u32> eye_y;
    };

    union {
        u32 word_3;

        BitField<29, 3, u32> mustache_type;
        BitField<24, 5, u32> eyebrow_type;
        BitField<21, 3, u32> beard_type;
        BitField<16, 5, u32> nose_type;
        BitField<13, 3, u32> mouth_aspect;
        BitField<8, 5, u32> nose_y;
        BitField<5, 3, u32> eyebrow_aspect;
        BitField<0, 5, u32> mouth_y;
    };

    union {
        u32 word_4;

        BitField<29, 3, u32> eye_rotate;
        BitField<24, 5, u32> mustache_y;
        BitField<21, 3, u32> eye_aspect;
        BitField<16, 5, u32> glasses_y;
        BitField<13, 3, u32> eye_scale;
        BitField<8, 5, u32> mole_x;
        BitField<0, 5, u32> mole_y;
    };

    union {
        u32 word_5;

        BitField<24, 5, u32> glasses_type;
        BitField<20, 4, u32> face_type;
        BitField<16, 4, u32> favorite_color;
        BitField<12, 4, u32> face_wrinkle;
        BitField<8, 4, u32> face_color;
        BitField<4, 4, u32> eye_x;
        BitField<0, 4, u32> face_makeup;
    };

    union {
        u32 word_6;

        BitField<28, 4, u32> eyebrow_rotate;
        BitField<24, 4, u32> eyebrow_scale;
        BitField<20, 4, u32> eyebrow_y;
        BitField<16, 4, u32> eyebrow_x;
        BitField<12, 4, u32> mouth_scale;
        BitField<8, 4, u32> nose_scale;
        BitField<4, 4, u32> mole_scale;
        BitField<0, 4, u32> mustache_scale;
    };
};
static_assert(sizeof(MiiStoreBitFields) == 0x1C, "MiiStoreBitFields has incorrect size.");
static_assert(std::is_trivially_copyable_v<MiiStoreBitFields>,
              "MiiStoreBitFields is not trivially copyable.");

struct MiiStoreData {
    // This corresponds to the above structure MiiStoreBitFields. I did it like this because the
    // BitField<> type makes this (and any thing that contains it) not trivially copyable, which is
    // not suitable for our uses.
    std::array<u8, 0x1C> data;
    static_assert(sizeof(MiiStoreBitFields) == sizeof(data), "data field has incorrect size.");

    std::array<char16_t, 10> name;
    Common::UUID uuid;
    u16 crc_1;
    u16 crc_2;

    std::u16string Name() const;
};
static_assert(sizeof(MiiStoreData) == 0x44, "MiiStoreData has incorrect size.");

struct MiiStoreDataElement {
    MiiStoreData data;
    Source source;
};
static_assert(sizeof(MiiStoreDataElement) == 0x48, "MiiStoreDataElement has incorrect size.");

struct MiiDatabase {
    u32 magic; // 'NFDB'
    std::array<MiiStoreData, MAX_MIIS> miis;
    INSERT_PADDING_BYTES(1);
    u8 count;
    u16 crc;
};
static_assert(sizeof(MiiDatabase) == 0x1A98, "MiiDatabase has incorrect size.");
#pragma pack(pop)

// The Mii manager is responsible for loading and storing the Miis to the database in NAND along
// with providing an easy interface for HLE emulation of the mii service.
class MiiManager {
public:
    MiiManager();
    ~MiiManager();

    MiiInfo CreateRandom(RandomParameters params);
    MiiInfo CreateDefault(u32 index);

    bool CheckUpdatedFlag() const;
    void ResetUpdatedFlag();

    bool IsTestModeEnabled() const;

    bool Empty() const;
    bool Full() const;

    void Clear();

    u32 Size() const;

    MiiInfo GetInfo(u32 index) const;
    MiiInfoElement GetInfoElement(u32 index) const;
    MiiStoreData GetStoreData(u32 index) const;
    MiiStoreDataElement GetStoreDataElement(u32 index) const;

    bool Remove(Common::UUID uuid);
    u32 IndexOf(Common::UUID uuid) const;
    u32 IndexOf(const MiiInfo& info) const;

    bool Move(Common::UUID uuid, u32 new_index);
    bool AddOrReplace(const MiiStoreData& data);

    bool DestroyFile();
    bool DeleteFile();

private:
    void WriteToFile();
    void ReadFromFile();

    MiiStoreData CreateMiiWithUniqueUUID() const;

    void EnsureDatabasePartition();

    MiiDatabase database;
    bool updated_flag = false;
    bool is_test_mode_enabled = false;
};

}; // namespace Service::Mii
