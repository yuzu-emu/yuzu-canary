// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/string_util.h"
#include "common/swap.h"
#include "core/file_sys/control_metadata.h"

namespace FileSys {

std::string LanguageEntry::GetApplicationName() const {
    return Common::StringFromFixedZeroTerminatedBuffer(application_name.data(), 0x200);
}

std::string LanguageEntry::GetDeveloperName() const {
    return Common::StringFromFixedZeroTerminatedBuffer(developer_name.data(), 0x100);
}

NACP::NACP(VirtualFile file) : raw(std::make_unique<RawNACP>()) {
    file->ReadObject(raw.get());
}

const LanguageEntry& NACP::GetLanguageEntry(Language language) const {
    return raw->language_entries.at(static_cast<u8>(language));
}

std::string NACP::GetApplicationName(Language language) const {
    return GetLanguageEntry(language).GetApplicationName();
}

std::string NACP::GetDeveloperName(Language language) const {
    return GetLanguageEntry(language).GetDeveloperName();
}

u64 NACP::GetTitleId() const {
    return raw->title_id;
}

std::string NACP::GetVersionString() const {
    return Common::StringFromFixedZeroTerminatedBuffer(raw->version_string.data(), 0x10);
}
} // namespace FileSys
