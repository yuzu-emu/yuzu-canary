// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
#include "core/loader/linker.h"
#include "core/loader/loader.h"

namespace FileSys {
class NACP;
}

namespace Loader {

/// Loads an NRO file
class AppLoader_NRO final : public AppLoader, Linker {
public:
    explicit AppLoader_NRO(FileSys::VirtualFile file);
    ~AppLoader_NRO() override;

    /**
     * Returns the type of the file
     * @param file std::shared_ptr<VfsFile> open file
     * @return FileType found, or FileType::Error if this loader doesn't know it
     */
    static FileType IdentifyType(const FileSys::VirtualFile& file);

    FileType GetFileType() override {
        return IdentifyType(file);
    }

    ResultStatus Load(Kernel::Process& process) override;

    ResultStatus ReadIcon(std::vector<u8>& buffer) override;
    ResultStatus ReadProgramId(u64& out_program_id) override;
    ResultStatus ReadRomFS(FileSys::VirtualFile& dir) override;
    ResultStatus ReadTitle(std::string& title) override;
    bool IsRomFSUpdatable() const override;

    static bool LoadNro(const std::vector<u8>& data, const std::string& name, VAddr load_base);

private:
    bool LoadNro(const FileSys::VfsFile& file, VAddr load_base);

    std::vector<u8> icon_data;
    std::unique_ptr<FileSys::NACP> nacp;
    FileSys::VirtualFile romfs;
};

} // namespace Loader
