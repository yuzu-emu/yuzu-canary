// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "core/file_sys/vfs.h"

namespace FileSys {

VirtualFile PatchIPS(const VirtualFile& in, const VirtualFile& ips);

} // namespace FileSys
