// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include "common/common_types.h"
#include "core/hle/result.h"

namespace FileSys {

enum class SaveDataSpaceId : u8 {
    NandSystem = 0,
    NandUser = 1,
    SdCard = 2,
    TemporaryStorage = 3,
};

enum class SaveDataType : u8 {
    SystemSaveData = 0,
    SaveData = 1,
    BcatDeliveryCacheStorage = 2,
    DeviceSaveData = 3,
    TemporaryStorage = 4,
    CacheStorage = 5,
};

struct SaveDataDescriptor {
    u64_le title_id;
    u128 user_id;
    u64_le save_id;
    SaveDataType type;
    INSERT_PADDING_BYTES(7);
    u64_le zero_1;
    u64_le zero_2;
    u64_le zero_3;

    std::string DebugInfo();
};
static_assert(sizeof(SaveDataDescriptor) == 0x40, "SaveDataDescriptor has incorrect size.");

/// File system interface to the SaveData archive
class SaveDataFactory {
public:
    explicit SaveDataFactory(VirtualDir dir);

    ResultVal<VirtualDir> Open(SaveDataSpaceId space, SaveDataDescriptor meta);

private:
    VirtualDir dir;

    std::string GetFullPath(SaveDataSpaceId space, SaveDataType type, u64 title_id, u128 user_id,
                            u64 save_id) const;
};

} // namespace FileSys
