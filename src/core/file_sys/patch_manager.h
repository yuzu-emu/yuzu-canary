// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <string>
#include "common/common_types.h"
#include "core/file_sys/vfs.h"
#include "nca_metadata.h"
#include "romfs_factory.h"

namespace FileSys {

class NCA;

enum class TitleVersionFormat : u8 {
    ThreeElements, ///< vX.Y.Z
    FourElements,  ///< vX.Y.Z.W
};

std::string FormatTitleVersion(u32 version,
                               TitleVersionFormat format = TitleVersionFormat::ThreeElements);

enum class PatchType {
    Update,
};

std::string FormatPatchTypeName(PatchType type);

// A centralized class to manage patches to games.
class PatchManager {
public:
    explicit PatchManager(u64 title_id);

    // Currently tracked ExeFS patches:
    // - Game Updates
    VirtualDir PatchExeFS(VirtualDir exefs) const;

    // Currently tracked RomFS patches:
    // - Game Updates
    VirtualFile PatchRomFS(VirtualFile base, u64 ivfc_offset,
                           ContentRecordType type = ContentRecordType::Program) const;

    // Returns a vector of pairs between patch names and patch versions.
    // i.e. Update v80 will return {Update, 80}
    std::map<PatchType, u32> GetPatchVersionNames() const;

private:
    u64 title_id;
};

} // namespace FileSys
