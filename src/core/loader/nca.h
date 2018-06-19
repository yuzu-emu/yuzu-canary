// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/loader.h"

namespace Loader {

class Nca;

/// Loads an NCA file
class AppLoader_NCA final : public AppLoader {
public:
    AppLoader_NCA(FileUtil::IOFile&& file, std::string filepath);

    /**
     * Returns the type of the file
     * @param file FileUtil::IOFile open file
     * @param filepath Path of the file that we are opening.
     * @return FileType found, or FileType::Error if this loader doesn't know it
     */
    static FileType IdentifyType(FileUtil::IOFile& file, const std::string& filepath);

    FileType GetFileType() override {
        return IdentifyType(file, filepath);
    }

    ResultStatus Load(Kernel::SharedPtr<Kernel::Process>& process) override;

    ResultStatus ReadRomFS(std::shared_ptr<FileUtil::IOFile>& romfs_file, u64& offset,
                           u64& size) override;

    ~AppLoader_NCA();

private:
    std::string filepath;
    FileSys::ProgramMetadata metadata;

    std::unique_ptr<Nca> nca;
};

} // namespace Loader
