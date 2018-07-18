// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace FileSys {

enum class Mode : u32 {
    Read = 1,
    Write = 2,
    Append = 4,
};

} // namespace FileSys
