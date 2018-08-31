// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs.h"
#include "core/loader/loader.h"
#include "romfs_factory.h"

namespace FileSys {

class NSP : public ReadOnlyVfsDirectory {
public:
    explicit NSP(VirtualFile file);
    ~NSP();

    Loader::ResultStatus GetStatus() const;
    Loader::ResultStatus GetProgramStatus(u64 title_id) const;
    // Should only be used when one title id can be assured.
    u64 GetFirstTitleID() const;
    u64 GetProgramTitleID() const;
    std::vector<u64> GetTitleIDs() const;

    bool IsExtractedType() const;

    // Common (Can be safely called on both types)
    VirtualFile GetRomFS() const;
    VirtualDir GetExeFS() const;

    // Type 0 Only (Collection of NCAs + Certificate + Ticket + Meta XML)
    std::vector<std::shared_ptr<NCA>> GetNCAsCollapsed() const;
    std::multimap<u64, std::shared_ptr<NCA>> GetNCAsByTitleID() const;
    std::map<u64, std::map<ContentRecordType, std::shared_ptr<NCA>>> GetNCAs() const;
    std::shared_ptr<NCA> GetNCA(u64 title_id, ContentRecordType type) const;
    VirtualFile GetNCAFile(u64 title_id, ContentRecordType type) const;
    std::vector<Core::Crypto::Key128> GetTitlekey() const;

    std::vector<VirtualFile> GetFiles() const override;

    std::vector<VirtualDir> GetSubdirectories() const override;

    std::string GetName() const override;

    VirtualDir GetParentDirectory() const override;

protected:
    bool ReplaceFileWithSubdirectory(VirtualFile file, VirtualDir dir) override;

private:
    VirtualFile file;

    bool extracted;
    Loader::ResultStatus status;
    std::map<u64, Loader::ResultStatus> program_status;

    std::shared_ptr<PartitionFilesystem> pfs;
    // Map title id -> {map type -> NCA}
    std::map<u64, std::map<ContentRecordType, std::shared_ptr<NCA>>> ncas;
    std::vector<VirtualFile> ticket_files;

    VirtualFile romfs;
    VirtualDir exefs;
};
} // namespace FileSys
