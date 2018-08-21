// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/loader/loader.h"

namespace FileSys {

class NAX;

} // namespace FileSys

namespace Loader {

class AppLoader_NCA;

/// Loads a NAX file
class AppLoader_NAX final : public AppLoader {
public:
    explicit AppLoader_NAX(FileSys::VirtualFile file);
    ~AppLoader_NAX() override;

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
    ResultStatus ReadProgramId(u64& out_program_id) override;

private:
    std::unique_ptr<FileSys::NAX> nax;
    std::unique_ptr<AppLoader_NCA> nca_loader;
};

} // namespace Loader
