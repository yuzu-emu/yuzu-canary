// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include "common/common_types.h"
#include "common/memory_hook.h"
#include "common/page_table.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace FileSys {
enum class ProgramAddressSpaceType : u8;
}

namespace Kernel {

enum class VMAType : u8 {
    /// VMA represents an unmapped region of the address space.
    Free,
    /// VMA is backed by a ref-counted allocate memory block.
    AllocatedMemoryBlock,
    /// VMA is backed by a raw, unmanaged pointer.
    BackingMemory,
    /// VMA is mapped to MMIO registers at a fixed PAddr.
    MMIO,
    // TODO(yuriks): Implement MemoryAlias to support MAP/UNMAP
};

/// Permissions for mapped memory blocks
enum class VMAPermission : u8 {
    None = 0,
    Read = 1,
    Write = 2,
    Execute = 4,

    ReadWrite = Read | Write,
    ReadExecute = Read | Execute,
    WriteExecute = Write | Execute,
    ReadWriteExecute = Read | Write | Execute,
};

constexpr VMAPermission operator|(VMAPermission lhs, VMAPermission rhs) {
    return static_cast<VMAPermission>(u32(lhs) | u32(rhs));
}

constexpr VMAPermission operator&(VMAPermission lhs, VMAPermission rhs) {
    return static_cast<VMAPermission>(u32(lhs) & u32(rhs));
}

constexpr VMAPermission operator^(VMAPermission lhs, VMAPermission rhs) {
    return static_cast<VMAPermission>(u32(lhs) ^ u32(rhs));
}

constexpr VMAPermission operator~(VMAPermission permission) {
    return static_cast<VMAPermission>(~u32(permission));
}

constexpr VMAPermission& operator|=(VMAPermission& lhs, VMAPermission rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr VMAPermission& operator&=(VMAPermission& lhs, VMAPermission rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr VMAPermission& operator^=(VMAPermission& lhs, VMAPermission rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

/// Attribute flags that can be applied to a VMA
enum class MemoryAttribute : u32 {
    Mask = 0xFF,

    /// No particular qualities
    None = 0,
    /// Memory locked/borrowed for use. e.g. This would be used by transfer memory.
    Locked = 1,
    /// Memory locked for use by IPC-related internals.
    LockedForIPC = 2,
    /// Mapped as part of the device address space.
    DeviceMapped = 4,
    /// Uncached memory
    Uncached = 8,
};

constexpr MemoryAttribute operator|(MemoryAttribute lhs, MemoryAttribute rhs) {
    return static_cast<MemoryAttribute>(u32(lhs) | u32(rhs));
}

constexpr MemoryAttribute operator&(MemoryAttribute lhs, MemoryAttribute rhs) {
    return static_cast<MemoryAttribute>(u32(lhs) & u32(rhs));
}

constexpr MemoryAttribute operator^(MemoryAttribute lhs, MemoryAttribute rhs) {
    return static_cast<MemoryAttribute>(u32(lhs) ^ u32(rhs));
}

constexpr MemoryAttribute operator~(MemoryAttribute attribute) {
    return static_cast<MemoryAttribute>(~u32(attribute));
}

constexpr MemoryAttribute& operator|=(MemoryAttribute& lhs, MemoryAttribute rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr MemoryAttribute& operator&=(MemoryAttribute& lhs, MemoryAttribute rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr MemoryAttribute& operator^=(MemoryAttribute& lhs, MemoryAttribute rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

constexpr u32 ToSvcMemoryAttribute(MemoryAttribute attribute) {
    return static_cast<u32>(attribute & MemoryAttribute::Mask);
}

// clang-format off
/// Represents memory states and any relevant flags, as used by the kernel.
/// svcQueryMemory interprets these by masking away all but the first eight
/// bits when storing memory state into a MemoryInfo instance.
enum class MemoryState : u32 {
    Mask                            = 0xFF,
    FlagProtect                     = 1U << 8,
    FlagDebug                       = 1U << 9,
    FlagIPC0                        = 1U << 10,
    FlagIPC3                        = 1U << 11,
    FlagIPC1                        = 1U << 12,
    FlagMapped                      = 1U << 13,
    FlagCode                        = 1U << 14,
    FlagAlias                       = 1U << 15,
    FlagModule                      = 1U << 16,
    FlagTransfer                    = 1U << 17,
    FlagQueryPhysicalAddressAllowed = 1U << 18,
    FlagSharedDevice                = 1U << 19,
    FlagSharedDeviceAligned         = 1U << 20,
    FlagIPCBuffer                   = 1U << 21,
    FlagMemoryPoolAllocated         = 1U << 22,
    FlagMapProcess                  = 1U << 23,
    FlagUncached                    = 1U << 24,
    FlagCodeMemory                  = 1U << 25,

    // Convenience flag sets to reduce repetition
    IPCFlags = FlagIPC0 | FlagIPC3 | FlagIPC1,

    CodeFlags = FlagDebug | IPCFlags | FlagMapped | FlagCode | FlagQueryPhysicalAddressAllowed |
                FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    DataFlags = FlagProtect | IPCFlags | FlagMapped | FlagAlias | FlagTransfer |
                FlagQueryPhysicalAddressAllowed | FlagSharedDevice | FlagSharedDeviceAligned |
                FlagMemoryPoolAllocated | FlagIPCBuffer | FlagUncached,

    Unmapped               = 0x00,
    Io                     = 0x01 | FlagMapped,
    Normal                 = 0x02 | FlagMapped | FlagQueryPhysicalAddressAllowed,
    CodeStatic             = 0x03 | CodeFlags  | FlagMapProcess,
    CodeMutable            = 0x04 | CodeFlags  | FlagMapProcess | FlagCodeMemory,
    Heap                   = 0x05 | DataFlags  | FlagCodeMemory,
    Shared                 = 0x06 | FlagMapped | FlagMemoryPoolAllocated,
    ModuleCodeStatic       = 0x08 | CodeFlags  | FlagModule | FlagMapProcess,
    ModuleCodeMutable      = 0x09 | DataFlags  | FlagModule | FlagMapProcess | FlagCodeMemory,

    IpcBuffer0             = 0x0A | FlagMapped | FlagQueryPhysicalAddressAllowed | FlagMemoryPoolAllocated |
                                    IPCFlags | FlagSharedDevice | FlagSharedDeviceAligned,

    Stack                  = 0x0B | FlagMapped | IPCFlags | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    ThreadLocal            = 0x0C | FlagMapped | FlagMemoryPoolAllocated,

    TransferMemoryIsolated = 0x0D | IPCFlags | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated |
                                    FlagUncached,

    TransferMemory         = 0x0E | FlagIPC3   | FlagIPC1   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    ProcessMemory          = 0x0F | FlagIPC3   | FlagIPC1   | FlagMapped | FlagMemoryPoolAllocated,

    // Used to signify an inaccessible or invalid memory region with memory queries
    Inaccessible           = 0x10,

    IpcBuffer1             = 0x11 | FlagIPC3   | FlagIPC1   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDevice | FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    IpcBuffer3             = 0x12 | FlagIPC3   | FlagMapped | FlagQueryPhysicalAddressAllowed |
                                    FlagSharedDeviceAligned | FlagMemoryPoolAllocated,

    KernelStack            = 0x13 | FlagMapped,
};
// clang-format on

constexpr MemoryState operator|(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) | u32(rhs));
}

constexpr MemoryState operator&(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) & u32(rhs));
}

constexpr MemoryState operator^(MemoryState lhs, MemoryState rhs) {
    return static_cast<MemoryState>(u32(lhs) ^ u32(rhs));
}

constexpr MemoryState operator~(MemoryState lhs) {
    return static_cast<MemoryState>(~u32(lhs));
}

constexpr MemoryState& operator|=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr MemoryState& operator&=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs & rhs;
    return lhs;
}

constexpr MemoryState& operator^=(MemoryState& lhs, MemoryState rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

constexpr u32 ToSvcMemoryState(MemoryState state) {
    return static_cast<u32>(state & MemoryState::Mask);
}

struct MemoryInfo {
    u64 base_address;
    u64 size;
    u32 state;
    u32 attributes;
    u32 permission;
    u32 ipc_ref_count;
    u32 device_ref_count;
};
static_assert(sizeof(MemoryInfo) == 0x28, "MemoryInfo has incorrect size.");

struct PageInfo {
    u32 flags;
};

/**
 * Represents a VMA in an address space. A VMA is a contiguous region of virtual addressing space
 * with homogeneous attributes across its extents. In this particular implementation each VMA is
 * also backed by a single host memory allocation.
 */
struct VirtualMemoryArea {
    /// Gets the starting (base) address of this VMA.
    VAddr StartAddress() const {
        return base;
    }

    /// Gets the ending address of this VMA.
    VAddr EndAddress() const {
        return base + size - 1;
    }

    /// Virtual base address of the region.
    VAddr base = 0;
    /// Size of the region.
    u64 size = 0;

    VMAType type = VMAType::Free;
    VMAPermission permissions = VMAPermission::None;
    MemoryState state = MemoryState::Unmapped;
    MemoryAttribute attribute = MemoryAttribute::None;

    // Settings for type = AllocatedMemoryBlock
    /// Memory block backing this VMA.
    std::shared_ptr<std::vector<u8>> backing_block = nullptr;
    /// Offset into the backing_memory the mapping starts from.
    std::size_t offset = 0;

    // Settings for type = BackingMemory
    /// Pointer backing this VMA. It will not be destroyed or freed when the VMA is removed.
    u8* backing_memory = nullptr;

    // Settings for type = MMIO
    /// Physical address of the register area this VMA maps to.
    PAddr paddr = 0;
    Common::MemoryHookPointer mmio_handler = nullptr;

    /// Tests if this area can be merged to the right with `next`.
    bool CanBeMergedWith(const VirtualMemoryArea& next) const;
};

/**
 * Manages a process' virtual addressing space. This class maintains a list of allocated and free
 * regions in the address space, along with their attributes, and allows kernel clients to
 * manipulate it, adjusting the page table to match.
 *
 * This is similar in idea and purpose to the VM manager present in operating system kernels, with
 * the main difference being that it doesn't have to support swapping or memory mapping of files.
 * The implementation is also simplified by not having to allocate page frames. See these articles
 * about the Linux kernel for an explantion of the concept and implementation:
 *  - http://duartes.org/gustavo/blog/post/how-the-kernel-manages-your-memory/
 *  - http://duartes.org/gustavo/blog/post/page-cache-the-affair-between-memory-and-files/
 */
class VMManager final {
    using VMAMap = std::map<VAddr, VirtualMemoryArea>;

public:
    using VMAHandle = VMAMap::const_iterator;

    VMManager();
    ~VMManager();

    /// Clears the address space map, re-initializing with a single free area.
    void Reset(FileSys::ProgramAddressSpaceType type);

    /// Finds the VMA in which the given address is included in, or `vma_map.end()`.
    VMAHandle FindVMA(VAddr target) const;

    /// Indicates whether or not the given handle is within the VMA map.
    bool IsValidHandle(VMAHandle handle) const;

    // TODO(yuriks): Should these functions actually return the handle?

    /**
     * Maps part of a ref-counted block of memory at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param block The block to be mapped.
     * @param offset Offset into `block` to map from.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     */
    ResultVal<VMAHandle> MapMemoryBlock(VAddr target, std::shared_ptr<std::vector<u8>> block,
                                        std::size_t offset, u64 size, MemoryState state);

    /**
     * Maps an unmanaged host memory pointer at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param memory The memory to be mapped.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     */
    ResultVal<VMAHandle> MapBackingMemory(VAddr target, u8* memory, u64 size, MemoryState state);

    /**
     * Finds the first free address that can hold a region of the desired size.
     *
     * @param size Size of the desired region.
     * @return The found free address.
     */
    ResultVal<VAddr> FindFreeRegion(u64 size) const;

    /**
     * Maps a memory-mapped IO region at a given address.
     *
     * @param target The guest address to start the mapping at.
     * @param paddr The physical address where the registers are present.
     * @param size Size of the mapping.
     * @param state MemoryState tag to attach to the VMA.
     * @param mmio_handler The handler that will implement read and write for this MMIO region.
     */
    ResultVal<VMAHandle> MapMMIO(VAddr target, PAddr paddr, u64 size, MemoryState state,
                                 Common::MemoryHookPointer mmio_handler);

    /// Unmaps a range of addresses, splitting VMAs as necessary.
    ResultCode UnmapRange(VAddr target, u64 size);

    /// Changes the permissions of the given VMA.
    VMAHandle Reprotect(VMAHandle vma, VMAPermission new_perms);

    /// Changes the permissions of a range of addresses, splitting VMAs as necessary.
    ResultCode ReprotectRange(VAddr target, u64 size, VMAPermission new_perms);

    ResultVal<VAddr> HeapAllocate(VAddr target, u64 size, VMAPermission perms);
    ResultCode HeapFree(VAddr target, u64 size);

    ResultCode MirrorMemory(VAddr dst_addr, VAddr src_addr, u64 size, MemoryState state);

    /// Queries the memory manager for information about the given address.
    ///
    /// @param address The address to query the memory manager about for information.
    ///
    /// @return A MemoryInfo instance containing information about the given address.
    ///
    MemoryInfo QueryMemory(VAddr address) const;

    /// Sets an attribute across the given address range.
    ///
    /// @param address   The starting address
    /// @param size      The size of the range to set the attribute on.
    /// @param mask      The attribute mask
    /// @param attribute The attribute to set across the given address range
    ///
    /// @returns RESULT_SUCCESS if successful
    /// @returns ERR_INVALID_ADDRESS_STATE if the attribute could not be set.
    ///
    ResultCode SetMemoryAttribute(VAddr address, u64 size, MemoryAttribute mask,
                                  MemoryAttribute attribute);

    /**
     * Scans all VMAs and updates the page table range of any that use the given vector as backing
     * memory. This should be called after any operation that causes reallocation of the vector.
     */
    void RefreshMemoryBlockMappings(const std::vector<u8>* block);

    /// Dumps the address space layout to the log, for debugging
    void LogLayout() const;

    /// Gets the total memory usage, used by svcGetInfo
    u64 GetTotalMemoryUsage() const;

    /// Gets the total heap usage, used by svcGetInfo
    u64 GetTotalHeapUsage() const;

    /// Gets the address space base address
    VAddr GetAddressSpaceBaseAddress() const;

    /// Gets the address space end address
    VAddr GetAddressSpaceEndAddress() const;

    /// Gets the total address space address size in bytes
    u64 GetAddressSpaceSize() const;

    /// Gets the address space width in bits.
    u64 GetAddressSpaceWidth() const;

    /// Determines whether or not the given address range lies within the address space.
    bool IsWithinAddressSpace(VAddr address, u64 size) const;

    /// Gets the base address of the ASLR region.
    VAddr GetASLRRegionBaseAddress() const;

    /// Gets the end address of the ASLR region.
    VAddr GetASLRRegionEndAddress() const;

    /// Gets the size of the ASLR region
    u64 GetASLRRegionSize() const;

    /// Determines whether or not the specified address range is within the ASLR region.
    bool IsWithinASLRRegion(VAddr address, u64 size) const;

    /// Gets the base address of the code region.
    VAddr GetCodeRegionBaseAddress() const;

    /// Gets the end address of the code region.
    VAddr GetCodeRegionEndAddress() const;

    /// Gets the total size of the code region in bytes.
    u64 GetCodeRegionSize() const;

    /// Determines whether or not the specified range is within the code region.
    bool IsWithinCodeRegion(VAddr address, u64 size) const;

    /// Gets the base address of the heap region.
    VAddr GetHeapRegionBaseAddress() const;

    /// Gets the end address of the heap region;
    VAddr GetHeapRegionEndAddress() const;

    /// Gets the total size of the heap region in bytes.
    u64 GetHeapRegionSize() const;

    /// Determines whether or not the specified range is within the heap region.
    bool IsWithinHeapRegion(VAddr address, u64 size) const;

    /// Gets the base address of the map region.
    VAddr GetMapRegionBaseAddress() const;

    /// Gets the end address of the map region.
    VAddr GetMapRegionEndAddress() const;

    /// Gets the total size of the map region in bytes.
    u64 GetMapRegionSize() const;

    /// Determines whether or not the specified range is within the map region.
    bool IsWithinMapRegion(VAddr address, u64 size) const;

    /// Gets the base address of the new map region.
    VAddr GetNewMapRegionBaseAddress() const;

    /// Gets the end address of the new map region.
    VAddr GetNewMapRegionEndAddress() const;

    /// Gets the total size of the new map region in bytes.
    u64 GetNewMapRegionSize() const;

    /// Determines whether or not the given address range is within the new map region
    bool IsWithinNewMapRegion(VAddr address, u64 size) const;

    /// Gets the base address of the TLS IO region.
    VAddr GetTLSIORegionBaseAddress() const;

    /// Gets the end address of the TLS IO region.
    VAddr GetTLSIORegionEndAddress() const;

    /// Gets the total size of the TLS IO region in bytes.
    u64 GetTLSIORegionSize() const;

    /// Determines if the given address range is within the TLS IO region.
    bool IsWithinTLSIORegion(VAddr address, u64 size) const;

    /// Each VMManager has its own page table, which is set as the main one when the owning process
    /// is scheduled.
    Common::PageTable page_table{Memory::PAGE_BITS};

private:
    using VMAIter = VMAMap::iterator;

    /// Converts a VMAHandle to a mutable VMAIter.
    VMAIter StripIterConstness(const VMAHandle& iter);

    /// Unmaps the given VMA.
    VMAIter Unmap(VMAIter vma);

    /**
     * Carves a VMA of a specific size at the specified address by splitting Free VMAs while doing
     * the appropriate error checking.
     */
    ResultVal<VMAIter> CarveVMA(VAddr base, u64 size);

    /**
     * Splits the edges of the given range of non-Free VMAs so that there is a VMA split at each
     * end of the range.
     */
    ResultVal<VMAIter> CarveVMARange(VAddr base, u64 size);

    /**
     * Splits a VMA in two, at the specified offset.
     * @returns the right side of the split, with the original iterator becoming the left side.
     */
    VMAIter SplitVMA(VMAIter vma, u64 offset_in_vma);

    /**
     * Checks for and merges the specified VMA with adjacent ones if possible.
     * @returns the merged VMA or the original if no merging was possible.
     */
    VMAIter MergeAdjacent(VMAIter vma);

    /// Updates the pages corresponding to this VMA so they match the VMA's attributes.
    void UpdatePageTableForVMA(const VirtualMemoryArea& vma);

    /// Initializes memory region ranges to adhere to a given address space type.
    void InitializeMemoryRegionRanges(FileSys::ProgramAddressSpaceType type);

    /// Clears the underlying map and page table.
    void Clear();

    /// Clears out the VMA map, unmapping any previously mapped ranges.
    void ClearVMAMap();

    /// Clears out the page table
    void ClearPageTable();

    using CheckResults = ResultVal<std::tuple<MemoryState, VMAPermission, MemoryAttribute>>;

    /// Checks if an address range adheres to the specified states provided.
    ///
    /// @param address         The starting address of the address range.
    /// @param size            The size of the address range.
    /// @param state_mask      The memory state mask.
    /// @param state           The state to compare the individual VMA states against,
    ///                        which is done in the form of: (vma.state & state_mask) != state.
    /// @param permission_mask The memory permissions mask.
    /// @param permissions     The permission to compare the individual VMA permissions against,
    ///                        which is done in the form of:
    ///                        (vma.permission & permission_mask) != permission.
    /// @param attribute_mask  The memory attribute mask.
    /// @param attribute       The memory attributes to compare the individual VMA attributes
    ///                        against, which is done in the form of:
    ///                        (vma.attributes & attribute_mask) != attribute.
    /// @param ignore_mask     The memory attributes to ignore during the check.
    ///
    /// @returns If successful, returns a tuple containing the memory attributes
    ///          (with ignored bits specified by ignore_mask unset), memory permissions, and
    ///          memory state across the memory range.
    /// @returns If not successful, returns ERR_INVALID_ADDRESS_STATE.
    ///
    CheckResults CheckRangeState(VAddr address, u64 size, MemoryState state_mask, MemoryState state,
                                 VMAPermission permission_mask, VMAPermission permissions,
                                 MemoryAttribute attribute_mask, MemoryAttribute attribute,
                                 MemoryAttribute ignore_mask) const;

    /**
     * A map covering the entirety of the managed address space, keyed by the `base` field of each
     * VMA. It must always be modified by splitting or merging VMAs, so that the invariant
     * `elem.base + elem.size == next.base` is preserved, and mergeable regions must always be
     * merged when possible so that no two similar and adjacent regions exist that have not been
     * merged.
     */
    VMAMap vma_map;

    u32 address_space_width = 0;
    VAddr address_space_base = 0;
    VAddr address_space_end = 0;

    VAddr aslr_region_base = 0;
    VAddr aslr_region_end = 0;

    VAddr code_region_base = 0;
    VAddr code_region_end = 0;

    VAddr heap_region_base = 0;
    VAddr heap_region_end = 0;

    VAddr map_region_base = 0;
    VAddr map_region_end = 0;

    VAddr new_map_region_base = 0;
    VAddr new_map_region_end = 0;

    VAddr tls_io_region_base = 0;
    VAddr tls_io_region_end = 0;

    // Memory used to back the allocations in the regular heap. A single vector is used to cover
    // the entire virtual address space extents that bound the allocations, including any holes.
    // This makes deallocation and reallocation of holes fast and keeps process memory contiguous
    // in the emulator address space, allowing Memory::GetPointer to be reasonably safe.
    std::shared_ptr<std::vector<u8>> heap_memory;
    // The left/right bounds of the address space covered by heap_memory.
    VAddr heap_start = 0;
    VAddr heap_end = 0;
    u64 heap_used = 0;
};
} // namespace Kernel
