// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "core/file_sys/vfs.h"
#include "core/loader/loader.h"

namespace FileSys {
class NCA;
}

namespace Loader {

class AppLoader_DeconstructedRomDirectory;

/// Loads an NCA file
class AppLoader_NCA final : public AppLoader {
public:
    explicit AppLoader_NCA(FileSys::VirtualFile file);
    ~AppLoader_NCA() override;

    /**
     * Returns the type of the file
     * @param file std::shared_ptr<VfsFile> open file
     * @return FileType found, or FileType::Error if this loader doesn't know it
     */
    static FileType IdentifyType(const FileSys::VirtualFile& file);

    FileType GetFileType() const override {
        return IdentifyType(file);
    }

    LoadResult Load(Kernel::Process& process) override;

    ResultStatus ReadRomFS(FileSys::VirtualFile& dir) override;
    u64 ReadRomFSIVFCOffset() const override;
    ResultStatus ReadProgramId(u64& out_program_id) override;

    ResultStatus ReadBanner(std::vector<u8>& buffer) override;
    ResultStatus ReadLogo(std::vector<u8>& buffer) override;

    ResultStatus ReadNSOModules(Modules& modules) override;

private:
    std::unique_ptr<FileSys::NCA> nca;
    std::unique_ptr<AppLoader_DeconstructedRomDirectory> directory_loader;
};

} // namespace Loader
