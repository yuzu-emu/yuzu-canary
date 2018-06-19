// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/program_metadata.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/linker.h"
#include "core/loader/loader.h"

namespace Loader {

// TODO(DarkLordZach): Add support for encrypted.
struct Nca {
    Nca(FileUtil::IOFile&& file, std::string path);

    FileSys::PartitionFilesystem GetPfs(u8 id);

    boost::optional<u8> GetExeFsPfsId();

    u64 GetExeFsFileOffset(const std::string& file_name);
    u64 GetExeFsFileSize(const std::string& file_name);

    u64 GetRomFsOffset();
    u64 GetRomFsSize();

    bool IsValid();

    std::vector<u8> GetExeFsFile(const std::string& file_name);

private:
    std::vector<FileSys::PartitionFilesystem> pfs;
    std::vector<u64> pfs_offset;

    u64 romfs_offset = 0;
    u64 romfs_size = 0;

    bool valid = false;

    FileUtil::IOFile file;
    std::string path;
};

/// Loads an NCA file
class AppLoader_NCA final : public AppLoader, Linker {
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

private:
    std::string filepath;
    FileSys::ProgramMetadata metadata;

    std::unique_ptr<Nca> nca;
};

} // namespace Loader
