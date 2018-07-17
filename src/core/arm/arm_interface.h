// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_types.h"
#include "core/hle/kernel/vm_manager.h"

/// Generic ARM11 CPU interface
class ARM_Interface : NonCopyable {
public:
    virtual ~ARM_Interface() {}

    struct ThreadContext {
        std::array<u64, 31> cpu_registers;
        u64 sp;
        u64 pc;
        u64 cpsr;
        std::array<u128, 32> fpu_registers;
        u64 fpscr;

        // TODO(bunnei): Fix once we have proper support for tpidrro_el0, etc. in the JIT
        VAddr tls_address;
    };

    /// Runs the CPU until an event happens
    virtual void Run() = 0;

    /// Step CPU by one instruction
    virtual void Step() = 0;

    /// Maps a backing memory region for the CPU
    virtual void MapBackingMemory(VAddr address, size_t size, u8* memory,
                                  Kernel::VMAPermission perms) = 0;

    /// Unmaps a region of memory that was previously mapped using MapBackingMemory
    virtual void UnmapMemory(VAddr address, size_t size) = 0;

    /// Clear all instruction cache
    virtual void ClearInstructionCache() = 0;

    /// Notify CPU emulation that page tables have changed
    virtual void PageTableChanged() = 0;

    /**
     * Set the Program Counter to an address
     * @param addr Address to set PC to
     */
    virtual void SetPC(u64 addr) = 0;

    /*
     * Get the current Program Counter
     * @return Returns current PC
     */
    virtual u64 GetPC() const = 0;

    /**
     * Get an ARM register
     * @param index Register index
     * @return Returns the value in the register
     */
    virtual u64 GetReg(int index) const = 0;

    /**
     * Set an ARM register
     * @param index Register index
     * @param value Value to set register to
     */
    virtual void SetReg(int index, u64 value) = 0;

    virtual u128 GetExtReg(int index) const = 0;

    virtual void SetExtReg(int index, u128 value) = 0;

    /**
     * Gets the value of a VFP register
     * @param index Register index (0-31)
     * @return Returns the value in the register
     */
    virtual u32 GetVFPReg(int index) const = 0;

    /**
     * Sets a VFP register to the given value
     * @param index Register index (0-31)
     * @param value Value to set register to
     */
    virtual void SetVFPReg(int index, u32 value) = 0;

    /**
     * Get the current CPSR register
     * @return Returns the value of the CPSR register
     */
    virtual u32 GetCPSR() const = 0;

    /**
     * Set the current CPSR register
     * @param cpsr Value to set CPSR to
     */
    virtual void SetCPSR(u32 cpsr) = 0;

    virtual VAddr GetTlsAddress() const = 0;

    virtual void SetTlsAddress(VAddr address) = 0;

    /**
     * Saves the current CPU context
     * @param ctx Thread context to save
     */
    virtual void SaveContext(ThreadContext& ctx) = 0;

    /**
     * Loads a CPU context
     * @param ctx Thread context to load
     */
    virtual void LoadContext(const ThreadContext& ctx) = 0;

    virtual void ClearExclusiveState() = 0;

    /// Prepare core for thread reschedule (if needed to correctly handle state)
    virtual void PrepareReschedule() = 0;
};
