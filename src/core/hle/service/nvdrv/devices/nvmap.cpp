// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/hle/service/nvdrv/devices/nvmap.h"

namespace Service::Nvidia::Devices {

namespace NvErrCodes {
enum {
    OperationNotPermitted = -1,
    InvalidValue = -22,
};
}

nvmap::nvmap(Core::System& system) : nvdevice(system) {}
nvmap::~nvmap() = default;

VAddr nvmap::GetObjectAddress(u32 handle) const {
    auto object = GetObject(handle);
    ASSERT(object);
    ASSERT(object->status == Object::Status::Allocated);
    return object->addr;
}

u32 nvmap::ioctl(Ioctl command, const std::vector<u8>& input, const std::vector<u8>& input2,
                 std::vector<u8>& output, std::vector<u8>& output2, IoctlCtrl& ctrl,
                 IoctlVersion version) {
    switch (static_cast<IoctlCommand>(command.raw)) {
    case IoctlCommand::Create:
        return IocCreate(input, output);
    case IoctlCommand::Alloc:
        return IocAlloc(input, output);
    case IoctlCommand::GetId:
        return IocGetId(input, output);
    case IoctlCommand::FromId:
        return IocFromId(input, output);
    case IoctlCommand::Param:
        return IocParam(input, output);
    case IoctlCommand::Free:
        return IocFree(input, output);
    }

    UNIMPLEMENTED_MSG("Unimplemented ioctl");
    return 0;
}

u32 nvmap::IocCreate(const std::vector<u8>& input, std::vector<u8>& output) {
    IocCreateParams params;
    std::memcpy(&params, input.data(), sizeof(params));
    LOG_DEBUG(Service_NVDRV, "size=0x{:08X}", params.size);

    if (!params.size) {
        LOG_ERROR(Service_NVDRV, "Size is 0");
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }
    // Create a new nvmap object and obtain a handle to it.
    auto object = std::make_shared<Object>();
    object->id = next_id++;
    object->size = params.size;
    object->status = Object::Status::Created;
    object->refcount = 1;

    u32 handle = next_handle++;
    handles[handle] = std::move(object);

    params.handle = handle;

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvmap::IocAlloc(const std::vector<u8>& input, std::vector<u8>& output) {
    IocAllocParams params;
    std::memcpy(&params, input.data(), sizeof(params));
    LOG_DEBUG(Service_NVDRV, "called, addr={:X}", params.addr);

    if (!params.handle) {
        LOG_ERROR(Service_NVDRV, "Handle is 0");
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    if ((params.align - 1) & params.align) {
        LOG_ERROR(Service_NVDRV, "Incorrect alignment used, alignment={:08X}", params.align);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    const u32 min_alignment = 0x1000;
    if (params.align < min_alignment) {
        params.align = min_alignment;
    }

    auto object = GetObject(params.handle);
    if (!object) {
        LOG_ERROR(Service_NVDRV, "Object does not exist, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    if (object->status == Object::Status::Allocated) {
        LOG_ERROR(Service_NVDRV, "Object is already allocated, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::OperationNotPermitted);
    }

    object->flags = params.flags;
    object->align = params.align;
    object->kind = params.kind;
    object->addr = params.addr;
    object->status = Object::Status::Allocated;

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvmap::IocGetId(const std::vector<u8>& input, std::vector<u8>& output) {
    IocGetIdParams params;
    std::memcpy(&params, input.data(), sizeof(params));

    LOG_WARNING(Service_NVDRV, "called");

    if (!params.handle) {
        LOG_ERROR(Service_NVDRV, "Handle is zero");
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    auto object = GetObject(params.handle);
    if (!object) {
        LOG_ERROR(Service_NVDRV, "Object does not exist, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::OperationNotPermitted);
    }

    params.id = object->id;

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvmap::IocFromId(const std::vector<u8>& input, std::vector<u8>& output) {
    IocFromIdParams params;
    std::memcpy(&params, input.data(), sizeof(params));

    LOG_WARNING(Service_NVDRV, "(STUBBED) called");

    auto itr = std::find_if(handles.begin(), handles.end(),
                            [&](const auto& entry) { return entry.second->id == params.id; });
    if (itr == handles.end()) {
        LOG_ERROR(Service_NVDRV, "Object does not exist, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    auto& object = itr->second;
    if (object->status != Object::Status::Allocated) {
        LOG_ERROR(Service_NVDRV, "Object is not allocated, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    itr->second->refcount++;

    // Return the existing handle instead of creating a new one.
    params.handle = itr->first;

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvmap::IocParam(const std::vector<u8>& input, std::vector<u8>& output) {
    enum class ParamTypes { Size = 1, Alignment = 2, Base = 3, Heap = 4, Kind = 5, Compr = 6 };

    IocParamParams params;
    std::memcpy(&params, input.data(), sizeof(params));

    LOG_WARNING(Service_NVDRV, "(STUBBED) called type={}", params.param);

    auto object = GetObject(params.handle);
    if (!object) {
        LOG_ERROR(Service_NVDRV, "Object does not exist, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    if (object->status != Object::Status::Allocated) {
        LOG_ERROR(Service_NVDRV, "Object is not allocated, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::OperationNotPermitted);
    }

    switch (static_cast<ParamTypes>(params.param)) {
    case ParamTypes::Size:
        params.result = object->size;
        break;
    case ParamTypes::Alignment:
        params.result = object->align;
        break;
    case ParamTypes::Heap:
        // TODO(Subv): Seems to be a hardcoded value?
        params.result = 0x40000000;
        break;
    case ParamTypes::Kind:
        params.result = object->kind;
        break;
    default:
        UNIMPLEMENTED();
    }

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

u32 nvmap::IocFree(const std::vector<u8>& input, std::vector<u8>& output) {
    // TODO(Subv): These flags are unconfirmed.
    enum FreeFlags {
        Freed = 0,
        NotFreedYet = 1,
    };

    IocFreeParams params;
    std::memcpy(&params, input.data(), sizeof(params));

    LOG_WARNING(Service_NVDRV, "(STUBBED) called");

    auto itr = handles.find(params.handle);
    if (itr == handles.end()) {
        LOG_ERROR(Service_NVDRV, "Object does not exist, handle={:08X}", params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }
    if (!itr->second->refcount) {
        LOG_ERROR(
            Service_NVDRV,
            "There is no references to this object. The object is already freed. handle={:08X}",
            params.handle);
        return static_cast<u32>(NvErrCodes::InvalidValue);
    }

    itr->second->refcount--;

    params.size = itr->second->size;

    if (itr->second->refcount == 0) {
        params.flags = Freed;
        // The address of the nvmap is written to the output if we're finally freeing it, otherwise
        // 0 is written.
        params.address = itr->second->addr;
    } else {
        params.flags = NotFreedYet;
        params.address = 0;
    }

    handles.erase(params.handle);

    std::memcpy(output.data(), &params, sizeof(params));
    return 0;
}

} // namespace Service::Nvidia::Devices
