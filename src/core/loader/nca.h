// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/loader.h"

namespace Loader {

/// Loads an NCA file
class AppLoader_NCA final : public AppLoader {
public:
    explicit AppLoader_NCA(FileSys::VirtualFile file);

    /**
     * Returns the type of the file
     * @param file std::shared_ptr<VfsFile> open file
     * @return FileType found, or FileType::Error if this loader doesn't know it
     */
    static FileType IdentifyType(const FileSys::VirtualFile& file);

    FileType GetFileType() override {
        return IdentifyType(file);
    }

    ResultStatus Load(Kernel::SharedPtr<Kernel::Process>& process) override;

    ResultStatus ReadRomFS(FileSys::VirtualFile& dir) override;

    ~AppLoader_NCA();

private:
    FileSys::ProgramMetadata metadata;

    std::unique_ptr<FileSys::NCA> nca;
};

} // namespace Loader
