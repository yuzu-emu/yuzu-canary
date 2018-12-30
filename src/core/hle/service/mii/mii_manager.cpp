// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include "common/assert.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hle/service/mii/mii_manager.h"

namespace Service::Mii {

namespace {

constexpr char MII_SAVE_DATABASE_PATH[] = "/system/save/8000000000000030/MiiDatabase.dat";
constexpr std::array<char16_t, 11> DEFAULT_MII_NAME = {u'y', u'u', u'z', u'u', u'\0'};

// This value was retrieved from HW test
constexpr MiiStoreData DEFAULT_MII = {
    {
        0x21, 0x40, 0x40, 0x01, 0x08, 0x01, 0x13, 0x08, 0x08, 0x02, 0x17, 0x8C, 0x06, 0x01,
        0x69, 0x6D, 0x8A, 0x6A, 0x82, 0x14, 0x00, 0x00, 0x00, 0x20, 0x64, 0x72, 0x44, 0x44,
    },
    {'y', 'u', 'z', 'u', '\0'},
    Common::UUID{1, 0},
    0,
    0,
};

// Default values taken from multiple real databases
const MiiDatabase DEFAULT_MII_DATABASE{Common::MakeMagic('N', 'F', 'D', 'B'), {}, {1}, 0, 0};

constexpr std::array<const char*, 4> SOURCE_NAMES{
    "Database",
    "Default",
    "Account",
    "Friend",
};

template <typename T, std::size_t SourceArraySize, std::size_t DestArraySize>
std::array<T, DestArraySize> ResizeArray(const std::array<T, SourceArraySize>& in) {
    std::array<T, DestArraySize> out{};
    std::memcpy(out.data(), in.data(), sizeof(T) * std::min(SourceArraySize, DestArraySize));
    return out;
}

MiiInfo ConvertStoreDataToInfo(const MiiStoreData& data) {
    MiiStoreBitFields bf{};
    std::memcpy(&bf, data.data.data(), sizeof(MiiStoreBitFields));
    return {
        data.uuid,
        ResizeArray<char16_t, 10, 11>(data.name),
        static_cast<u8>(bf.font_region.Value()),
        static_cast<u8>(bf.favorite_color.Value()),
        static_cast<u8>(bf.gender.Value()),
        static_cast<u8>(bf.height.Value()),
        static_cast<u8>(bf.weight.Value()),
        static_cast<u8>(bf.mii_type.Value()),
        static_cast<u8>(bf.mii_region.Value()),
        static_cast<u8>(bf.face_type.Value()),
        static_cast<u8>(bf.face_color.Value()),
        static_cast<u8>(bf.face_wrinkle.Value()),
        static_cast<u8>(bf.face_makeup.Value()),
        static_cast<u8>(bf.hair_type.Value()),
        static_cast<u8>(bf.hair_color.Value()),
        static_cast<bool>(bf.hair_flip.Value()),
        static_cast<u8>(bf.eye_type.Value()),
        static_cast<u8>(bf.eye_color.Value()),
        static_cast<u8>(bf.eye_scale.Value()),
        static_cast<u8>(bf.eye_aspect.Value()),
        static_cast<u8>(bf.eye_rotate.Value()),
        static_cast<u8>(bf.eye_x.Value()),
        static_cast<u8>(bf.eye_y.Value()),
        static_cast<u8>(bf.eyebrow_type.Value()),
        static_cast<u8>(bf.eyebrow_color.Value()),
        static_cast<u8>(bf.eyebrow_scale.Value()),
        static_cast<u8>(bf.eyebrow_aspect.Value()),
        static_cast<u8>(bf.eyebrow_rotate.Value()),
        static_cast<u8>(bf.eyebrow_x.Value()),
        static_cast<u8>(bf.eyebrow_y.Value()),
        static_cast<u8>(bf.nose_type.Value()),
        static_cast<u8>(bf.nose_scale.Value()),
        static_cast<u8>(bf.nose_y.Value()),
        static_cast<u8>(bf.mouth_type.Value()),
        static_cast<u8>(bf.mouth_color.Value()),
        static_cast<u8>(bf.mouth_scale.Value()),
        static_cast<u8>(bf.mouth_aspect.Value()),
        static_cast<u8>(bf.mouth_y.Value()),
        static_cast<u8>(bf.facial_hair_color.Value()),
        static_cast<u8>(bf.beard_type.Value()),
        static_cast<u8>(bf.mustache_type.Value()),
        static_cast<u8>(bf.mustache_scale.Value()),
        static_cast<u8>(bf.mustache_y.Value()),
        static_cast<u8>(bf.glasses_type.Value()),
        static_cast<u8>(bf.glasses_color.Value()),
        static_cast<u8>(bf.glasses_scale.Value()),
        static_cast<u8>(bf.glasses_y.Value()),
        static_cast<u8>(bf.mole_type.Value()),
        static_cast<u8>(bf.mole_scale.Value()),
        static_cast<u8>(bf.mole_x.Value()),
        static_cast<u8>(bf.mole_y.Value()),
        0x00,
    };
}
MiiStoreData ConvertInfoToStoreData(const MiiInfo& info) {
    MiiStoreData out{};
    out.name = ResizeArray<char16_t, 11, 10>(info.name);
    out.uuid = info.uuid;

    MiiStoreBitFields bf{};

    bf.hair_type.Assign(info.hair_type);
    bf.mole_type.Assign(info.mole_type);
    bf.height.Assign(info.height);
    bf.hair_flip.Assign(info.hair_flip);
    bf.weight.Assign(info.weight);
    bf.hair_color.Assign(info.hair_color);

    bf.gender.Assign(info.gender);
    bf.eye_color.Assign(info.eye_color);
    bf.eyebrow_color.Assign(info.eyebrow_color);
    bf.mouth_color.Assign(info.mouth_color);
    bf.facial_hair_color.Assign(info.facial_hair_color);

    bf.mii_type.Assign(info.mii_type);
    bf.glasses_color.Assign(info.glasses_color);
    bf.font_region.Assign(info.font_region);
    bf.eye_type.Assign(info.eye_type);
    bf.mii_region.Assign(info.mii_region);
    bf.mouth_type.Assign(info.mouth_type);
    bf.glasses_scale.Assign(info.glasses_scale);
    bf.eye_y.Assign(info.eye_y);

    bf.mustache_type.Assign(info.mustache_type);
    bf.eyebrow_type.Assign(info.eyebrow_type);
    bf.beard_type.Assign(info.beard_type);
    bf.nose_type.Assign(info.nose_type);
    bf.mouth_aspect.Assign(info.mouth_aspect_ratio);
    bf.nose_y.Assign(info.nose_y);
    bf.eyebrow_aspect.Assign(info.eyebrow_aspect_ratio);
    bf.mouth_y.Assign(info.mouth_y);

    bf.eye_rotate.Assign(info.eye_rotate);
    bf.mustache_y.Assign(info.mustache_y);
    bf.eye_aspect.Assign(info.eye_aspect_ratio);
    bf.glasses_y.Assign(info.glasses_y);
    bf.eye_scale.Assign(info.eye_scale);
    bf.mole_x.Assign(info.mole_x);
    bf.mole_y.Assign(info.mole_y);

    bf.glasses_type.Assign(info.glasses_type);
    bf.face_type.Assign(info.face_type);
    bf.favorite_color.Assign(info.favorite_color);
    bf.face_wrinkle.Assign(info.face_wrinkle);
    bf.face_color.Assign(info.face_color);
    bf.eye_x.Assign(info.eye_x);
    bf.face_makeup.Assign(info.face_makeup);

    bf.eyebrow_rotate.Assign(info.eyebrow_rotate);
    bf.eyebrow_scale.Assign(info.eyebrow_scale);
    bf.eyebrow_y.Assign(info.eyebrow_y);
    bf.eyebrow_x.Assign(info.eyebrow_x);
    bf.mouth_scale.Assign(info.mouth_scale);
    bf.nose_scale.Assign(info.nose_scale);
    bf.mole_scale.Assign(info.mole_scale);
    bf.mustache_scale.Assign(info.mustache_scale);

    std::memcpy(out.data.data(), &bf, sizeof(MiiStoreBitFields));

    return out;
}

} // namespace

std::ostream& operator<<(std::ostream& os, Source source) {
    os << SOURCE_NAMES.at(static_cast<std::size_t>(source));
    return os;
}

std::u16string MiiInfo::Name() const {
    return Common::UTF16StringFromFixedZeroTerminatedBuffer(name.data(), name.size());
}

bool operator==(const MiiInfo& lhs, const MiiInfo& rhs) {
    return std::memcmp(&lhs, &rhs, sizeof(MiiInfo)) == 0;
}

bool operator!=(const MiiInfo& lhs, const MiiInfo& rhs) {
    return !operator==(lhs, rhs);
}

std::u16string MiiStoreData::Name() const {
    return Common::UTF16StringFromFixedZeroTerminatedBuffer(name.data(), name.size());
}

MiiManager::MiiManager() = default;

MiiManager::~MiiManager() = default;

MiiInfo MiiManager::CreateRandom(RandomParameters params) {
    LOG_WARNING(Service_Mii,
                "(STUBBED) called with params={:08X}{:08X}{:08X}, returning default Mii",
                params.unknown_1, params.unknown_2, params.unknown_3);

    return ConvertStoreDataToInfo(CreateMiiWithUniqueUUID());
}

MiiInfo MiiManager::CreateDefault(u32 index) {
    const auto new_mii = CreateMiiWithUniqueUUID();

    database.miis.at(index) = new_mii;

    EnsureDatabasePartition();
    return ConvertStoreDataToInfo(new_mii);
}

bool MiiManager::CheckUpdatedFlag() const {
    return updated_flag;
}

void MiiManager::ResetUpdatedFlag() {
    updated_flag = false;
}

bool MiiManager::IsTestModeEnabled() const {
    return is_test_mode_enabled;
}

bool MiiManager::Empty() const {
    return Size() == 0;
}

bool MiiManager::Full() const {
    return Size() == MAX_MIIS;
}

void MiiManager::Clear() {
    updated_flag = true;
    std::fill(database.miis.begin(), database.miis.end(), MiiStoreData{});
}

u32 MiiManager::Size() const {
    return static_cast<u32>(std::count_if(database.miis.begin(), database.miis.end(),
                                          [](const MiiStoreData& elem) { return elem.uuid; }));
}

MiiInfo MiiManager::GetInfo(u32 index) const {
    return ConvertStoreDataToInfo(GetStoreData(index));
}

MiiInfoElement MiiManager::GetInfoElement(u32 index) const {
    return {GetInfo(index), Source::Database};
}

MiiStoreData MiiManager::GetStoreData(u32 index) const {
    return database.miis.at(index);
}

MiiStoreDataElement MiiManager::GetStoreDataElement(u32 index) const {
    return {GetStoreData(index), Source::Database};
}

bool MiiManager::Remove(Common::UUID uuid) {
    const auto iter = std::find_if(database.miis.begin(), database.miis.end(),
                                   [uuid](const MiiStoreData& elem) { return elem.uuid == uuid; });

    if (iter == database.miis.end())
        return false;

    updated_flag = true;
    *iter = MiiStoreData{};
    EnsureDatabasePartition();
    return true;
}

u32 MiiManager::IndexOf(Common::UUID uuid) const {
    const auto iter = std::find_if(database.miis.begin(), database.miis.end(),
                                   [uuid](const MiiStoreData& elem) { return elem.uuid == uuid; });

    if (iter == database.miis.end())
        return INVALID_INDEX;

    return static_cast<u32>(std::distance(database.miis.begin(), iter));
}

u32 MiiManager::IndexOf(const MiiInfo& info) const {
    const auto iter =
        std::find_if(database.miis.begin(), database.miis.end(), [&info](const MiiStoreData& elem) {
            return ConvertStoreDataToInfo(elem) == info;
        });

    if (iter == database.miis.end())
        return INVALID_INDEX;

    return static_cast<u32>(std::distance(database.miis.begin(), iter));
}

bool MiiManager::Move(Common::UUID uuid, u32 new_index) {
    const auto index = IndexOf(uuid);

    if (index == INVALID_INDEX || new_index >= MAX_MIIS)
        return false;

    updated_flag = true;
    const auto moving = database.miis[index];
    const auto replacing = database.miis[new_index];
    if (replacing.uuid) {
        database.miis[index] = replacing;
        database.miis[new_index] = moving;
    } else {
        database.miis[index] = MiiStoreData{};
        database.miis[new_index] = moving;
    }

    EnsureDatabasePartition();
    return true;
}

bool MiiManager::AddOrReplace(const MiiStoreData& data) {
    const auto index = IndexOf(data.uuid);

    updated_flag = true;
    if (index == INVALID_INDEX) {
        const auto size = Size();
        if (size == MAX_MIIS)
            return false;
        database.miis[size] = data;
    } else {
        database.miis[index] = data;
    }

    return true;
}

bool MiiManager::DestroyFile() {
    database = DEFAULT_MII_DATABASE;
    updated_flag = false;
    return DeleteFile();
}

bool MiiManager::DeleteFile() {
    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + MII_SAVE_DATABASE_PATH;
    return FileUtil::Exists(path) && FileUtil::Delete(path);
}

void MiiManager::WriteToFile() {
    const auto raw_path =
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "/system/save/8000000000000030";
    if (FileUtil::Exists(raw_path) && !FileUtil::IsDirectory(raw_path))
        FileUtil::Delete(raw_path);

    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + MII_SAVE_DATABASE_PATH;

    if (!FileUtil::CreateFullPath(path)) {
        LOG_WARNING(Service_Mii,
                    "Failed to create full path of MiiDatabase.dat. Create the directory "
                    "nand/system/save/8000000000000030 to mitigate this "
                    "issue.");
        return;
    }

    FileUtil::IOFile save(path, "wb");

    if (!save.IsOpen()) {
        LOG_WARNING(Service_Mii, "Failed to write save data to file... No changes to user data "
                                 "made in current session will be saved.");
        return;
    }

    save.Resize(sizeof(MiiDatabase));
    if (save.WriteBytes(&database, sizeof(MiiDatabase)) != sizeof(MiiDatabase)) {
        LOG_WARNING(Service_Mii, "Failed to write all data to save file... Data may be malformed "
                                 "and/or regenerated on next run.");
        save.Resize(0);
    }
}

void MiiManager::ReadFromFile() {
    FileUtil::IOFile save(
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + MII_SAVE_DATABASE_PATH, "rb");

    if (!save.IsOpen()) {
        LOG_WARNING(Service_ACC, "Failed to load profile data from save data... Generating new "
                                 "blank Mii database with no Miis.");
        std::memcpy(&database, &DEFAULT_MII_DATABASE, sizeof(MiiDatabase));
        return;
    }

    if (save.ReadBytes(&database, sizeof(MiiDatabase)) != sizeof(MiiDatabase)) {
        LOG_WARNING(Service_ACC, "MiiDatabase.dat is smaller than expected... Generating new blank "
                                 "Mii database with no Miis.");
        std::memcpy(&database, &DEFAULT_MII_DATABASE, sizeof(MiiDatabase));
        return;
    }

    EnsureDatabasePartition();
}

MiiStoreData MiiManager::CreateMiiWithUniqueUUID() const {
    auto new_mii = DEFAULT_MII;

    do {
        new_mii.uuid = Common::UUID::Generate();
    } while (IndexOf(new_mii.uuid) == INVALID_INDEX);

    return new_mii;
}

void MiiManager::EnsureDatabasePartition() {
    std::stable_partition(database.miis.begin(), database.miis.end(),
                          [](const MiiStoreData& elem) { return elem.uuid; });
}

} // namespace Service::Mii
