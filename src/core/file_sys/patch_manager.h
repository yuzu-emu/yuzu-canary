// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <string>
#include "common/common_types.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/vfs.h"

namespace FileSys {

class NCA;
class NACP;

enum class TitleVersionFormat : u8 {
    ThreeElements, ///< vX.Y.Z
    FourElements,  ///< vX.Y.Z.W
};

std::string FormatTitleVersion(u32 version,
                               TitleVersionFormat format = TitleVersionFormat::ThreeElements);

enum class PatchType {
    Update,
    LayeredFS,
};

std::string FormatPatchTypeName(PatchType type);

// A centralized class to manage patches to games.
class PatchManager {
public:
    explicit PatchManager(u64 title_id);
    ~PatchManager();

    // Currently tracked ExeFS patches:
    // - Game Updates
    VirtualDir PatchExeFS(VirtualDir exefs) const;

    // Currently tracked RomFS patches:
    // - Game Updates
    // - LayeredFS
    VirtualFile PatchRomFS(VirtualFile base, u64 ivfc_offset,
                           ContentRecordType type = ContentRecordType::Program) const;

    // Returns a vector of pairs between patch names and patch versions.
    // i.e. Update v80 will return {Update, 80}
    std::map<PatchType, std::string> GetPatchVersionNames() const;

    // Given title_id of the program, attempts to get the control data of the update and parse it,
    // falling back to the base control data.
    std::pair<std::shared_ptr<NACP>, VirtualFile> GetControlMetadata() const;

    // Version of GetControlMetadata that takes an arbitrary NCA
    std::pair<std::shared_ptr<NACP>, VirtualFile> ParseControlNCA(
        const std::shared_ptr<NCA>& nca) const;

private:
    u64 title_id;
};

} // namespace FileSys
