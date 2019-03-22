// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/object.h"

namespace Kernel {

Object::Object(KernelCore& kernel) : kernel{kernel}, object_id{kernel.CreateNewObjectID()} {}
Object::~Object() = default;

bool Object::IsWaitable() const {
    switch (GetHandleType()) {
    case HandleType::ReadableEvent:
    case HandleType::Thread:
    case HandleType::Process:
    case HandleType::ServerPort:
    case HandleType::ServerSession:
        return true;

    case HandleType::Unknown:
    case HandleType::WritableEvent:
    case HandleType::SharedMemory:
    case HandleType::TransferMemory:
    case HandleType::AddressArbiter:
    case HandleType::ResourceLimit:
    case HandleType::ClientPort:
    case HandleType::ClientSession:
        return false;
    }

    UNREACHABLE();
    return false;
}

} // namespace Kernel
