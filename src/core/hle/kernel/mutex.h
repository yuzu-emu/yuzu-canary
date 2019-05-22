// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

union ResultCode;

namespace Core {
class System;
}

namespace Kernel {

class Mutex final {
public:
    explicit Mutex(Core::System& system);
    ~Mutex();

    /// Flag that indicates that a mutex still has threads waiting for it.
    static constexpr u32 MutexHasWaitersFlag = 0x40000000;
    /// Mask of the bits in a mutex address value that contain the mutex owner.
    static constexpr u32 MutexOwnerMask = 0xBFFFFFFF;

    /// Attempts to acquire a mutex at the specified address.
    ResultCode TryAcquire(VAddr address, Handle holding_thread_handle,
                          Handle requesting_thread_handle);

    /// Releases the mutex at the specified address.
    ResultCode Release(VAddr address, Thread* holding_thread);

private:
    Core::System& system;
};

} // namespace Kernel
