// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "core/file_sys/patch_manager.h"
#include "core/loader/linker.h"
#include "core/loader/loader.h"

namespace Loader {

/// Loads an NSO file
class AppLoader_NSO final : public AppLoader, Linker {
public:
    explicit AppLoader_NSO(FileSys::VirtualFile file);

    /**
     * Returns the type of the file
     * @param file std::shared_ptr<VfsFile> open file
     * @return FileType found, or FileType::Error if this loader doesn't know it
     */
    static FileType IdentifyType(const FileSys::VirtualFile& file);

    FileType GetFileType() override {
        return IdentifyType(file);
    }

    static VAddr LoadModule(FileSys::VirtualFile file, VAddr load_base,
                            boost::optional<FileSys::PatchManager> pm = boost::none);

    ResultStatus Load(Kernel::Process& process) override;
};

} // namespace Loader
