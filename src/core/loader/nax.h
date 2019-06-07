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

    FileType GetFileType() const override;

    LoadResult Load(Kernel::Process& process) override;

    ResultStatus ReadRomFS(FileSys::VirtualFile& dir) override;
    u64 ReadRomFSIVFCOffset() const override;
    ResultStatus ReadProgramId(u64& out_program_id) override;

    ResultStatus ReadBanner(std::vector<u8>& buffer) override;
    ResultStatus ReadLogo(std::vector<u8>& buffer) override;

    ResultStatus ReadNSOModules(Modules& modules) override;

private:
    std::unique_ptr<FileSys::NAX> nax;
    std::unique_ptr<AppLoader_NCA> nca_loader;
};

} // namespace Loader
