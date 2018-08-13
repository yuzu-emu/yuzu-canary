// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <string>
#include "common/common_funcs.h"
#include "core/file_sys/vfs.h"

namespace FileSys {

// A localized entry containing strings within the NACP.
// One for each language of type Language.
struct LanguageEntry {
    std::array<char, 0x200> application_name;
    std::array<char, 0x100> developer_name;

    std::string GetApplicationName() const;
    std::string GetDeveloperName() const;
};
static_assert(sizeof(LanguageEntry) == 0x300, "LanguageEntry has incorrect size.");

// The raw file format of a NACP file.
struct RawNACP {
    std::array<LanguageEntry, 16> language_entries;
    INSERT_PADDING_BYTES(0x38);
    u64_le title_id;
    INSERT_PADDING_BYTES(0x20);
    std::array<char, 0x10> version_string;
    u64_le dlc_base_title_id;
    u64_le title_id_2;
    INSERT_PADDING_BYTES(0x28);
    u64_le product_code;
    u64_le title_id_3;
    std::array<u64_le, 0x7> title_id_array;
    INSERT_PADDING_BYTES(0x8);
    u64_le title_id_update;
    std::array<u8, 0x40> bcat_passphrase;
    INSERT_PADDING_BYTES(0xEC0);
};
static_assert(sizeof(RawNACP) == 0x4000, "RawNACP has incorrect size.");

// A language on the NX. These are for names and icons.
enum class Language : u8 {
    AmericanEnglish = 0,
    BritishEnglish = 1,
    Japanese = 2,
    French = 3,
    German = 4,
    LatinAmericanSpanish = 5,
    Spanish = 6,
    Italian = 7,
    Dutch = 8,
    CanadianFrench = 9,
    Portugese = 10,
    Russian = 11,
    Korean = 12,
    Taiwanese = 13,
    Chinese = 14,
};

static constexpr std::array<const char*, 15> LANGUAGE_NAMES = {
    "AmericanEnglish", "BritishEnglish", "Japanese",
    "French",          "German",         "LatinAmericanSpanish",
    "Spanish",         "Italian",        "Dutch",
    "CanadianFrench",  "Portugese",      "Russian",
    "Korean",          "Taiwanese",      "Chinese"};

// A class representing the format used by NX metadata files, typically named Control.nacp.
// These store application name, dev name, title id, and other miscellaneous data.
class NACP {
public:
    explicit NACP(VirtualFile file);
    const LanguageEntry& GetLanguageEntry(Language language = Language::AmericanEnglish) const;
    std::string GetApplicationName(Language language = Language::AmericanEnglish) const;
    std::string GetDeveloperName(Language language = Language::AmericanEnglish) const;
    u64 GetTitleId() const;
    std::string GetVersionString() const;

private:
    std::unique_ptr<RawNACP> raw;
};

} // namespace FileSys
