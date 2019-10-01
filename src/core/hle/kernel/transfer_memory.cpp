// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/kernel/transfer_memory.h"
#include "core/hle/result.h"

namespace Kernel {

TransferMemory::TransferMemory(KernelCore& kernel) : Object{kernel} {}
TransferMemory::~TransferMemory() = default;

SharedPtr<TransferMemory> TransferMemory::Create(KernelCore& kernel, VAddr base_address, u64 size,
                                                 MemoryPermission permissions) {
    SharedPtr<TransferMemory> transfer_memory{new TransferMemory(kernel)};

    transfer_memory->base_address = base_address;
    transfer_memory->memory_size = size;
    transfer_memory->owner_permissions = permissions;
    transfer_memory->owner_process = kernel.CurrentProcess();

    transfer_memory->MapMemory(base_address, size, permissions);

    return transfer_memory;
}

const u8* TransferMemory::GetPointer() const {
    return backing_block.get()->data();
}

u64 TransferMemory::GetSize() const {
    return memory_size;
}

ResultCode TransferMemory::MapMemory(VAddr address, u64 size, MemoryPermission permissions) {
    if (memory_size != size) {
        return ERR_INVALID_SIZE;
    }

    if (owner_permissions != permissions) {
        return ERR_INVALID_STATE;
    }

    if (is_mapped) {
        return ERR_INVALID_STATE;
    }

    backing_block = std::make_shared<PhysicalMemory>(size);

    const auto map_state = owner_permissions == MemoryPermission::None
                               ? MemoryState::TransferMemoryIsolated
                               : MemoryState::TransferMemory;
    auto& vm_manager = owner_process->VMManager();
    const auto map_result = vm_manager.MapMemoryBlock(address, backing_block, 0, size, map_state);
    if (map_result.Failed()) {
        return map_result.Code();
    }

    is_mapped = true;
    return RESULT_SUCCESS;
}

ResultCode TransferMemory::UnmapMemory(VAddr address, u64 size) {
    if (memory_size != size) {
        return ERR_INVALID_SIZE;
    }

    auto& vm_manager = owner_process->VMManager();
    const auto result = vm_manager.UnmapRange(address, size);

    if (result.IsError()) {
        return result;
    }

    is_mapped = false;
    return RESULT_SUCCESS;
}

} // namespace Kernel
