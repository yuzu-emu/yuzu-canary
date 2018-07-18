// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/result.h"

namespace FileSys {

namespace ErrCodes {
enum {
    NotFound = 1,
    TitleNotFound = 1002,
    SdCardNotFound = 2001,
    RomFSNotFound = 2520,
};
}

constexpr ResultCode ERROR_PATH_NOT_FOUND(ErrorModule::FS, ErrCodes::NotFound);

// TODO(bunnei): Replace these with correct errors for Switch OS
constexpr ResultCode ERROR_INVALID_PATH(ResultCode(-1));
constexpr ResultCode ERROR_UNSUPPORTED_OPEN_FLAGS(ResultCode(-1));
constexpr ResultCode ERROR_INVALID_OPEN_FLAGS(ResultCode(-1));
constexpr ResultCode ERROR_FILE_NOT_FOUND(ResultCode(-1));
constexpr ResultCode ERROR_UNEXPECTED_FILE_OR_DIRECTORY(ResultCode(-1));
constexpr ResultCode ERROR_DIRECTORY_ALREADY_EXISTS(ResultCode(-1));
constexpr ResultCode ERROR_FILE_ALREADY_EXISTS(ResultCode(-1));
constexpr ResultCode ERROR_DIRECTORY_NOT_EMPTY(ResultCode(-1));

} // namespace FileSys
