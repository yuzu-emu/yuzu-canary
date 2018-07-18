// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include "common/common_funcs.h"
#include "common/common_types.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

enum EntryType : u8 {
    Directory = 0,
    File = 1,
};

// Structure of a directory entry, from
// http://switchbrew.org/index.php?title=Filesystem_services#DirectoryEntry
const size_t FILENAME_LENGTH = 0x300;
struct Entry {
    char filename[FILENAME_LENGTH];
    INSERT_PADDING_BYTES(4);
    EntryType type;
    INSERT_PADDING_BYTES(3);
    u64 file_size;
};
static_assert(sizeof(Entry) == 0x310, "Directory Entry struct isn't exactly 0x310 bytes long!");
static_assert(offsetof(Entry, type) == 0x304, "Wrong offset for type in Entry.");
static_assert(offsetof(Entry, file_size) == 0x308, "Wrong offset for file_size in Entry.");

class DirectoryBackend : NonCopyable {
public:
    DirectoryBackend() {}
    virtual ~DirectoryBackend() {}

    /**
     * List files contained in the directory
     * @param count Number of entries to return at once in entries
     * @param entries Buffer to read data into
     * @return Number of entries listed
     */
    virtual u64 Read(const u64 count, Entry* entries) = 0;

    /// Returns the number of entries still left to read.
    virtual u64 GetEntryCount() const = 0;

    /**
     * Close the directory
     * @return true if the directory closed correctly
     */
    virtual bool Close() const = 0;
};

} // namespace FileSys
