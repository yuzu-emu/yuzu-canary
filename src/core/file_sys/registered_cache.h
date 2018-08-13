// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <boost/container/flat_map.hpp>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/vfs.h"

namespace FileSys {
class XCI;
class CNMT;

using NcaID = std::array<u8, 0x10>;
using RegisteredCacheParsingFunction = std::function<VirtualFile(const VirtualFile&, const NcaID&)>;
using VfsCopyFunction = std::function<bool(VirtualFile, VirtualFile)>;

enum class InstallResult {
    Success,
    ErrorAlreadyExists,
    ErrorCopyFailed,
    ErrorMetaFailed,
};

struct RegisteredCacheEntry {
    u64 title_id;
    ContentRecordType type;

    std::string DebugInfo() const;
};

// boost flat_map requires operator< for O(log(n)) lookups.
bool operator<(const RegisteredCacheEntry& lhs, const RegisteredCacheEntry& rhs);

/*
 * A class that catalogues NCAs in the registered directory structure.
 * Nintendo's registered format follows this structure:
 *
 * Root
 *   | 000000XX <- XX is the ____ two digits of the NcaID
 *       | <hash>.nca <- hash is the NcaID (first half of SHA256 over entire file) (folder)
 *         | 00
 *         | 01 <- Actual content split along 4GB boundaries. (optional)
 *
 * (This impl also supports substituting the nca dir for an nca file, as that's more convenient when
 * 4GB splitting can be ignored.)
 */
class RegisteredCache {
public:
    // Parsing function defines the conversion from raw file to NCA. If there are other steps
    // besides creating the NCA from the file (e.g. NAX0 on SD Card), that should go in a custom
    // parsing function.
    explicit RegisteredCache(VirtualDir dir,
                             RegisteredCacheParsingFunction parsing_function =
                                 [](const VirtualFile& file, const NcaID& id) { return file; });

    void Refresh();

    bool HasEntry(u64 title_id, ContentRecordType type) const;
    bool HasEntry(RegisteredCacheEntry entry) const;

    VirtualFile GetEntryRaw(u64 title_id, ContentRecordType type) const;
    VirtualFile GetEntryRaw(RegisteredCacheEntry entry) const;

    std::shared_ptr<NCA> GetEntry(u64 title_id, ContentRecordType type) const;
    std::shared_ptr<NCA> GetEntry(RegisteredCacheEntry entry) const;

    std::vector<RegisteredCacheEntry> ListEntries() const;
    // If a parameter is not boost::none, it will be filtered for from all entries.
    std::vector<RegisteredCacheEntry> ListEntriesFilter(
        boost::optional<TitleType> title_type = boost::none,
        boost::optional<ContentRecordType> record_type = boost::none,
        boost::optional<u64> title_id = boost::none) const;

    // Raw copies all the ncas from the xci to the csache. Does some quick checks to make sure there
    // is a meta NCA and all of them are accessible.
    InstallResult InstallEntry(std::shared_ptr<XCI> xci, bool overwrite_if_exists = false,
                               const VfsCopyFunction& copy = &VfsRawCopy);

    // Due to the fact that we must use Meta-type NCAs to determine the existance of files, this
    // poses quite a challenge. Instead of creating a new meta NCA for this file, yuzu will create a
    // dir inside the NAND called 'yuzu_meta' and store the raw CNMT there.
    // TODO(DarkLordZach): Author real meta-type NCAs and install those.
    InstallResult InstallEntry(std::shared_ptr<NCA> nca, TitleType type,
                               bool overwrite_if_exists = false,
                               const VfsCopyFunction& copy = &VfsRawCopy);

private:
    template <typename T>
    void IterateAllMetadata(std::vector<T>& out,
                            std::function<T(const CNMT&, const ContentRecord&)> proc,
                            std::function<bool(const CNMT&, const ContentRecord&)> filter) const;
    std::vector<NcaID> AccumulateFiles() const;
    void ProcessFiles(const std::vector<NcaID>& ids);
    void AccumulateYuzuMeta();
    boost::optional<NcaID> GetNcaIDFromMetadata(u64 title_id, ContentRecordType type) const;
    VirtualFile GetFileAtID(NcaID id) const;
    VirtualFile OpenFileOrDirectoryConcat(const VirtualDir& dir, std::string_view path) const;
    InstallResult RawInstallNCA(std::shared_ptr<NCA> nca, const VfsCopyFunction& copy,
                                bool overwrite_if_exists,
                                boost::optional<NcaID> override_id = boost::none);
    bool RawInstallYuzuMeta(const CNMT& cnmt);

    VirtualDir dir;
    RegisteredCacheParsingFunction parser;
    // maps tid -> NcaID of meta
    boost::container::flat_map<u64, NcaID> meta_id;
    // maps tid -> meta
    boost::container::flat_map<u64, CNMT> meta;
    // maps tid -> meta for CNMT in yuzu_meta
    boost::container::flat_map<u64, CNMT> yuzu_meta;
};

} // namespace FileSys
