// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/file_sys/card_image.h"
#include "core/loader/loader.h"
#include "core/loader/nca.h"

namespace Loader {

/// Loads an XCI file
class AppLoader_XCI final : public AppLoader {
public:
    explicit AppLoader_XCI(FileSys::VirtualFile file);
    ~AppLoader_XCI();

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
    ResultStatus ReadIcon(std::vector<u8>& buffer) override;
    ResultStatus ReadTitle(std::string& title) override;

private:
    FileSys::ProgramMetadata metadata;

    std::unique_ptr<FileSys::XCI> xci;
    std::unique_ptr<AppLoader_NCA> nca_loader;

    FileSys::VirtualFile icon_file;
    std::shared_ptr<FileSys::NACP> nacp_file;
};

} // namespace Loader
