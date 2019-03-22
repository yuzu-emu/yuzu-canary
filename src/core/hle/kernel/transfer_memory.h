// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/kernel/object.h"

union ResultCode;

namespace Kernel {

class KernelCore;
class Process;

enum class MemoryPermission : u32;

/// Defines the interface for transfer memory objects.
///
/// Transfer memory is typically used for the purpose of
/// transferring memory between separate process instances,
/// thus the name.
///
class TransferMemory final : public Object {
public:
    static constexpr HandleType HANDLE_TYPE = HandleType::TransferMemory;

    static SharedPtr<TransferMemory> Create(KernelCore& kernel, VAddr base_address, size_t size,
                                            MemoryPermission permissions);

    TransferMemory(const TransferMemory&) = delete;
    TransferMemory& operator=(const TransferMemory&) = delete;

    TransferMemory(TransferMemory&&) = delete;
    TransferMemory& operator=(TransferMemory&&) = delete;

    std::string GetTypeName() const override {
        return "TransferMemory";
    }

    std::string GetName() const override {
        return GetTypeName();
    }

    HandleType GetHandleType() const override {
        return HANDLE_TYPE;
    }

    /// Attempts to map transfer memory with the given range and memory permissions.
    ///
    /// @param address     The base address to being mapping memory at.
    /// @param size        The size of the memory to map, in bytes.
    /// @param permissions The memory permissions to check against when mapping memory.
    ///
    /// @pre The given address, size, and memory permissions must all match
    ///      the same values that were given when creating the transfer memory
    ///      instance.
    ///
    ResultCode MapMemory(VAddr address, size_t size, MemoryPermission permissions);

    /// Unmaps the transfer memory with the given range
    ///
    /// @param address The base address to begin unmapping memory at.
    /// @param size    The size of the memory to unmap, in bytes.
    ///
    /// @pre The given address and size must be the same as the ones used
    ///      to create the transfer memory instance.
    ///
    ResultCode UnmapMemory(VAddr address, size_t size);

private:
    explicit TransferMemory(KernelCore& kernel);
    ~TransferMemory() override;

    /// The base address for the memory managed by this instance.
    VAddr base_address = 0;

    /// Size of the memory, in bytes, that this instance manages.
    size_t memory_size = 0;

    /// The memory permissions that are applied to this instance.
    MemoryPermission owner_permissions{};

    /// The process that this transfer memory instance was created under.
    Process* owner_process = nullptr;

    /// Whether or not this transfer memory instance has mapped memory.
    bool is_mapped = false;
};

} // namespace Kernel
