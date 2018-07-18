// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/kernel/process.h"

namespace FileSys {

std::string SaveDataDescriptor::DebugInfo() {
    return fmt::format("[type={:02X}, title_id={:016X}, user_id={:016X}{:016X}, save_id={:016X}]",
                       static_cast<u8>(type), title_id, user_id[1], user_id[0], save_id);
}

SaveDataFactory::SaveDataFactory(VirtualDir save_directory) : dir(std::move(save_directory)) {}

ResultVal<VirtualDir> SaveDataFactory::Open(SaveDataSpaceId space, SaveDataDescriptor meta) {
    if (meta.type == SaveDataType::SystemSaveData || meta.type == SaveDataType::SaveData) {
        if (meta.zero_1 != 0) {
            LOG_WARNING(Service_FS,
                        "Possibly incorrect SaveDataDescriptor, type is "
                        "SystemSaveData||SaveData but offset 0x28 is non-zero ({:016X}).",
                        meta.zero_1);
        }
        if (meta.zero_2 != 0) {
            LOG_WARNING(Service_FS,
                        "Possibly incorrect SaveDataDescriptor, type is "
                        "SystemSaveData||SaveData but offset 0x30 is non-zero ({:016X}).",
                        meta.zero_2);
        }
        if (meta.zero_3 != 0) {
            LOG_WARNING(Service_FS,
                        "Possibly incorrect SaveDataDescriptor, type is "
                        "SystemSaveData||SaveData but offset 0x38 is non-zero ({:016X}).",
                        meta.zero_3);
        }
    }

    if (meta.type == SaveDataType::SystemSaveData && meta.title_id != 0) {
        LOG_WARNING(Service_FS,
                    "Possibly incorrect SaveDataDescriptor, type is SystemSaveData but title_id is "
                    "non-zero ({:016X}).",
                    meta.title_id);
    }

    std::string save_directory =
        GetFullPath(space, meta.type, meta.title_id, meta.user_id, meta.save_id);

    // TODO(DarkLordZach): Try to not create when opening, there are dedicated create save methods.
    // But, user_ids don't match so this works for now.

    auto out = dir->GetDirectoryRelative(save_directory);

    if (out == nullptr) {
        // TODO(bunnei): This is a work-around to always create a save data directory if it does not
        // already exist. This is a hack, as we do not understand yet how this works on hardware.
        // Without a save data directory, many games will assert on boot. This should not have any
        // bad side-effects.
        out = dir->CreateDirectoryRelative(save_directory);
    }

    // Return an error if the save data doesn't actually exist.
    if (out == nullptr) {
        // TODO(Subv): Find out correct error code.
        return ResultCode(-1);
    }

    return MakeResult<VirtualDir>(std::move(out));
}

std::string SaveDataFactory::GetFullPath(SaveDataSpaceId space, SaveDataType type, u64 title_id,
                                         u128 user_id, u64 save_id) const {
    // According to switchbrew, if a save is of type SaveData and the title id field is 0, it should
    // be interpreted as the title id of the current process.
    if (type == SaveDataType::SaveData && title_id == 0)
        title_id = Core::CurrentProcess()->program_id;

    std::string out;

    switch (space) {
    case SaveDataSpaceId::NandSystem:
        out = "/system/save/";
        break;
    case SaveDataSpaceId::NandUser:
        out = "/user/save/";
        break;
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataSpaceId: {:02X}", static_cast<u8>(space));
    }

    switch (type) {
    case SaveDataType::SystemSaveData:
        return fmt::format("{}{:016X}/{:016X}{:016X}", out, save_id, user_id[1], user_id[0]);
    case SaveDataType::SaveData:
        return fmt::format("{}{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0],
                           title_id);
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataType: {:02X}", static_cast<u8>(type));
    }
}

} // namespace FileSys
