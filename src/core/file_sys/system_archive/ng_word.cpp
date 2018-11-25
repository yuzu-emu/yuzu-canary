// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "common/common_types.h"
#include "core/file_sys/system_archive/ng_word.h"
#include "core/file_sys/vfs_vector.h"

namespace FileSys::SystemArchive {

namespace NgWord1Data {

constexpr std::size_t NUMBER_WORD_TXT_FILES = 0x10;

// Should this archive replacement mysteriously not work on a future game, consider updating.
constexpr std::array<u8, 4> VERSION_DAT{0x0, 0x0, 0x0, 0x19}; // 5.1.0 System Version

constexpr std::array<u8, 30> WORD_TXT{
    0xFE, 0xFF, 0x00, 0x5E, 0x00, 0x76, 0x00, 0x65, 0x00, 0x72, 0x00, 0x79, 0x00, 0x62, 0x00,
    0x61, 0x00, 0x64, 0x00, 0x77, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x64, 0x00, 0x24, 0x00, 0x0A,
}; // "^verybadword$" in UTF-16

} // namespace NgWord1Data

VirtualDir NgWord1() {
    std::vector<VirtualFile> files(NgWord1Data::NUMBER_WORD_TXT_FILES);

    for (std::size_t i = 0; i < NgWord1Data::NUMBER_WORD_TXT_FILES; ++i) {
        files[i] = std::make_shared<ArrayVfsFile<NgWord1Data::WORD_TXT.size()>>(
            NgWord1Data::WORD_TXT, fmt::format("{}.txt", i));
    }

    files.push_back(std::make_shared<ArrayVfsFile<NgWord1Data::WORD_TXT.size()>>(
        NgWord1Data::WORD_TXT, "common.txt"));
    files.push_back(std::make_shared<ArrayVfsFile<NgWord1Data::VERSION_DAT.size()>>(
        NgWord1Data::VERSION_DAT, "version.dat"));

    return std::make_shared<VectorVfsDirectory>(files, std::vector<VirtualDir>{}, "data");
}

} // namespace FileSys::SystemArchive
