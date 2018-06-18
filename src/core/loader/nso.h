// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/linker.h"
#include "core/loader/loader.h"

namespace Loader {

/// Loads an NSO file
class AppLoader_NSO final : public AppLoader, Linker {
public:
    AppLoader_NSO(FileUtil::IOFile&& file, std::string filepath);

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

    static VAddr LoadModule(const std::string& name, const std::vector<u8>& file_data,
                            VAddr load_base);

    static VAddr LoadModule(const std::string& path, VAddr load_base);

    ResultStatus Load(Kernel::SharedPtr<Kernel::Process>& process) override;

private:
    std::string filepath;
};

} // namespace Loader
