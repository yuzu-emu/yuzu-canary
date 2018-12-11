// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <utility>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/memory.h"

namespace Kernel {

SharedMemory::SharedMemory(KernelCore& kernel) : Object{kernel} {}
SharedMemory::~SharedMemory() = default;

SharedPtr<SharedMemory> SharedMemory::Create(KernelCore& kernel, SharedPtr<Process> owner_process,
                                             u64 size, MemoryPermission permissions,
                                             MemoryPermission other_permissions, VAddr address,
                                             MemoryRegion region, std::string name) {
    SharedPtr<SharedMemory> shared_memory(new SharedMemory(kernel));

    shared_memory->owner_process = owner_process.get();
    shared_memory->name = std::move(name);
    shared_memory->size = size;
    shared_memory->permissions = permissions;
    shared_memory->other_permissions = other_permissions;

    if (address == 0) {
        shared_memory->backing_block = std::make_shared<std::vector<u8>>(size);
        shared_memory->backing_block_offset = 0;

        // Refresh the address mappings for the current process.
        if (Core::CurrentProcess() != nullptr) {
            Core::CurrentProcess()->VMManager().RefreshMemoryBlockMappings(
                shared_memory->backing_block.get());
        }
    } else {
        const auto& vm_manager = shared_memory->owner_process->VMManager();

        // The memory is already available and mapped in the owner process.
        const auto vma = vm_manager.FindVMA(address);
        ASSERT_MSG(vm_manager.IsValidHandle(vma), "Invalid memory address");
        ASSERT_MSG(vma->second.backing_block, "Backing block doesn't exist for address");

        // The returned VMA might be a bigger one encompassing the desired address.
        const auto vma_offset = address - vma->first;
        ASSERT_MSG(vma_offset + size <= vma->second.size,
                   "Shared memory exceeds bounds of mapped block");

        shared_memory->backing_block = vma->second.backing_block;
        shared_memory->backing_block_offset = vma->second.offset + vma_offset;
    }

    shared_memory->base_address = address;

    return shared_memory;
}

SharedPtr<SharedMemory> SharedMemory::CreateForApplet(
    KernelCore& kernel, std::shared_ptr<std::vector<u8>> heap_block, std::size_t offset, u64 size,
    MemoryPermission permissions, MemoryPermission other_permissions, std::string name) {
    SharedPtr<SharedMemory> shared_memory(new SharedMemory(kernel));

    shared_memory->owner_process = nullptr;
    shared_memory->name = std::move(name);
    shared_memory->size = size;
    shared_memory->permissions = permissions;
    shared_memory->other_permissions = other_permissions;
    shared_memory->backing_block = std::move(heap_block);
    shared_memory->backing_block_offset = offset;
    shared_memory->base_address =
        kernel.CurrentProcess()->VMManager().GetHeapRegionBaseAddress() + offset;

    return shared_memory;
}

ResultCode SharedMemory::Map(Process& target_process, VAddr address, MemoryPermission permissions,
                             MemoryPermission other_permissions) {
    const MemoryPermission own_other_permissions =
        &target_process == owner_process ? this->permissions : this->other_permissions;

    // Automatically allocated memory blocks can only be mapped with other_permissions = DontCare
    if (base_address == 0 && other_permissions != MemoryPermission::DontCare) {
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    // Error out if the requested permissions don't match what the creator process allows.
    if (static_cast<u32>(permissions) & ~static_cast<u32>(own_other_permissions)) {
        LOG_ERROR(Kernel, "cannot map id={}, address=0x{:X} name={}, permissions don't match",
                  GetObjectId(), address, name);
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    // Error out if the provided permissions are not compatible with what the creator process needs.
    if (other_permissions != MemoryPermission::DontCare &&
        static_cast<u32>(this->permissions) & ~static_cast<u32>(other_permissions)) {
        LOG_ERROR(Kernel, "cannot map id={}, address=0x{:X} name={}, permissions don't match",
                  GetObjectId(), address, name);
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    VAddr target_address = address;

    // Map the memory block into the target process
    auto result = target_process.VMManager().MapMemoryBlock(
        target_address, backing_block, backing_block_offset, size, MemoryState::Shared);
    if (result.Failed()) {
        LOG_ERROR(
            Kernel,
            "cannot map id={}, target_address=0x{:X} name={}, error mapping to virtual memory",
            GetObjectId(), target_address, name);
        return result.Code();
    }

    return target_process.VMManager().ReprotectRange(target_address, size,
                                                     ConvertPermissions(permissions));
}

ResultCode SharedMemory::Unmap(Process& target_process, VAddr address) {
    // TODO(Subv): Verify what happens if the application tries to unmap an address that is not
    // mapped to a SharedMemory.
    return target_process.VMManager().UnmapRange(address, size);
}

VMAPermission SharedMemory::ConvertPermissions(MemoryPermission permission) {
    u32 masked_permissions =
        static_cast<u32>(permission) & static_cast<u32>(MemoryPermission::ReadWriteExecute);
    return static_cast<VMAPermission>(masked_permissions);
}

u8* SharedMemory::GetPointer(std::size_t offset) {
    return backing_block->data() + backing_block_offset + offset;
}

const u8* SharedMemory::GetPointer(std::size_t offset) const {
    return backing_block->data() + backing_block_offset + offset;
}

} // namespace Kernel
