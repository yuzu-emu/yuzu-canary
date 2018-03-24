// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <boost/icl/interval_map.hpp>
#include <boost/optional.hpp>
#include "common/common_types.h"
#include "core/memory_hook.h"

namespace Kernel {
class Process;
}

namespace Memory {

/**
 * Page size used by the ARM architecture. This is the smallest granularity with which memory can
 * be mapped.
 */
constexpr size_t PAGE_BITS = 12;
constexpr u64 PAGE_SIZE = 1 << PAGE_BITS;
constexpr u64 PAGE_MASK = PAGE_SIZE - 1;
constexpr size_t ADDRESS_SPACE_BITS = 36;
constexpr size_t PAGE_TABLE_NUM_ENTRIES = 1ULL << (ADDRESS_SPACE_BITS - PAGE_BITS);

enum class PageType : u8 {
    /// Page is unmapped and should cause an access error.
    Unmapped,
    /// Page is mapped to regular memory. This is the only type you can get pointers to.
    Memory,
    /// Page is mapped to regular memory, but also needs to check for rasterizer cache flushing and
    /// invalidation
    RasterizerCachedMemory,
    /// Page is mapped to a I/O region. Writing and reading to this page is handled by functions.
    Special,
};

struct SpecialRegion {
    enum class Type {
        DebugHook,
        IODevice,
    } type;

    MemoryHookPointer handler;

    bool operator<(const SpecialRegion& other) const {
        return std::tie(type, handler) < std::tie(other.type, other.handler);
    }

    bool operator==(const SpecialRegion& other) const {
        return std::tie(type, handler) == std::tie(other.type, other.handler);
    }
};

/**
 * A (reasonably) fast way of allowing switchable and remappable process address spaces. It loosely
 * mimics the way a real CPU page table works.
 */
struct PageTable {
    /**
     * Array of memory pointers backing each page. An entry can only be non-null if the
     * corresponding entry in the `attributes` array is of type `Memory`.
     */
    std::array<u8*, PAGE_TABLE_NUM_ENTRIES> pointers;

    /**
     * Contains MMIO handlers that back memory regions whose entries in the `attribute` array is of
     * type `Special`.
     */
    boost::icl::interval_map<VAddr, std::set<SpecialRegion>> special_regions;

    /**
     * Array of fine grained page attributes. If it is set to any value other than `Memory`, then
     * the corresponding entry in `pointers` MUST be set to null.
     */
    std::array<PageType, PAGE_TABLE_NUM_ENTRIES> attributes;
};

/// Physical memory regions as seen from the ARM11
enum : PAddr {
    /// IO register area
    IO_AREA_PADDR = 0x10100000,
    IO_AREA_SIZE = 0x01000000, ///< IO area size (16MB)
    IO_AREA_PADDR_END = IO_AREA_PADDR + IO_AREA_SIZE,

    /// MPCore internal memory region
    MPCORE_RAM_PADDR = 0x17E00000,
    MPCORE_RAM_SIZE = 0x00002000, ///< MPCore internal memory size (8KB)
    MPCORE_RAM_PADDR_END = MPCORE_RAM_PADDR + MPCORE_RAM_SIZE,

    /// Video memory
    VRAM_PADDR = 0x18000000,
    VRAM_SIZE = 0x00600000, ///< VRAM size (6MB)
    VRAM_PADDR_END = VRAM_PADDR + VRAM_SIZE,

    /// DSP memory
    DSP_RAM_PADDR = 0x1FF00000,
    DSP_RAM_SIZE = 0x00080000, ///< DSP memory size (512KB)
    DSP_RAM_PADDR_END = DSP_RAM_PADDR + DSP_RAM_SIZE,

    /// AXI WRAM
    AXI_WRAM_PADDR = 0x1FF80000,
    AXI_WRAM_SIZE = 0x00080000, ///< AXI WRAM size (512KB)
    AXI_WRAM_PADDR_END = AXI_WRAM_PADDR + AXI_WRAM_SIZE,

    /// Main FCRAM
    FCRAM_PADDR = 0x20000000,
    FCRAM_SIZE = 0x08000000,      ///< FCRAM size on the Old 3DS (128MB)
    FCRAM_N3DS_SIZE = 0x10000000, ///< FCRAM size on the New 3DS (256MB)
    FCRAM_PADDR_END = FCRAM_PADDR + FCRAM_SIZE,
};

/// Virtual user-space memory regions
enum : VAddr {
    /// Where the application text, data and bss reside.
    PROCESS_IMAGE_VADDR = 0x08000000,
    PROCESS_IMAGE_MAX_SIZE = 0x08000000,
    PROCESS_IMAGE_VADDR_END = PROCESS_IMAGE_VADDR + PROCESS_IMAGE_MAX_SIZE,

    /// Maps 1:1 to an offset in FCRAM. Used for HW allocations that need to be linear in physical
    /// memory.
    LINEAR_HEAP_VADDR = 0x14000000,
    LINEAR_HEAP_SIZE = 0x08000000,
    LINEAR_HEAP_VADDR_END = LINEAR_HEAP_VADDR + LINEAR_HEAP_SIZE,

    /// Maps 1:1 to the IO register area.
    IO_AREA_VADDR = 0x1EC00000,
    IO_AREA_VADDR_END = IO_AREA_VADDR + IO_AREA_SIZE,

    /// Maps 1:1 to VRAM.
    VRAM_VADDR = 0x1F000000,
    VRAM_VADDR_END = VRAM_VADDR + VRAM_SIZE,

    /// Maps 1:1 to DSP memory.
    DSP_RAM_VADDR = 0x1FF00000,
    DSP_RAM_VADDR_END = DSP_RAM_VADDR + DSP_RAM_SIZE,

    /// Read-only page containing kernel and system configuration values.
    CONFIG_MEMORY_VADDR = 0x1FF80000,
    CONFIG_MEMORY_SIZE = 0x00001000,
    CONFIG_MEMORY_VADDR_END = CONFIG_MEMORY_VADDR + CONFIG_MEMORY_SIZE,

    /// Usually read-only page containing mostly values read from hardware.
    SHARED_PAGE_VADDR = 0x1FF81000,
    SHARED_PAGE_SIZE = 0x00001000,
    SHARED_PAGE_VADDR_END = SHARED_PAGE_VADDR + SHARED_PAGE_SIZE,

    /// Equivalent to LINEAR_HEAP_VADDR, but expanded to cover the extra memory in the New 3DS.
    NEW_LINEAR_HEAP_VADDR = 0x30000000,
    NEW_LINEAR_HEAP_SIZE = 0x10000000,
    NEW_LINEAR_HEAP_VADDR_END = NEW_LINEAR_HEAP_VADDR + NEW_LINEAR_HEAP_SIZE,

    /// Area where TLS (Thread-Local Storage) buffers are allocated.
    TLS_AREA_VADDR = NEW_LINEAR_HEAP_VADDR_END,
    TLS_ENTRY_SIZE = 0x200,
    TLS_AREA_SIZE = 0x10000000,
    TLS_ADREA_VADDR_END = TLS_AREA_VADDR + TLS_AREA_SIZE,

    /// Application stack
    STACK_VADDR = TLS_ADREA_VADDR_END,
    STACK_SIZE = 0x10000,
    STACK_VADDR_END = STACK_VADDR + STACK_SIZE,

    /// Application heap
    /// Size is confirmed to be a static value on fw 3.0.0
    HEAP_VADDR = 0x108000000,
    HEAP_SIZE = 0x180000000,
    HEAP_VADDR_END = HEAP_VADDR + HEAP_SIZE,

    /// New map region
    /// Size is confirmed to be a static value on fw 3.0.0
    NEW_MAP_REGION_VADDR = HEAP_VADDR_END,
    NEW_MAP_REGION_SIZE = 0x80000000,
    NEW_MAP_REGION_VADDR_END = NEW_MAP_REGION_VADDR + NEW_MAP_REGION_SIZE,

    /// Map region
    /// Size is confirmed to be a static value on fw 3.0.0
    MAP_REGION_VADDR = NEW_MAP_REGION_VADDR_END,
    MAP_REGION_SIZE = 0x1000000000,
    MAP_REGION_VADDR_END = MAP_REGION_VADDR + MAP_REGION_SIZE,
};

/// Currently active page table
void SetCurrentPageTable(PageTable* page_table);
PageTable* GetCurrentPageTable();

/// Determines if the given VAddr is valid for the specified process.
bool IsValidVirtualAddress(const Kernel::Process& process, const VAddr vaddr);
bool IsValidVirtualAddress(const VAddr addr);

bool IsValidPhysicalAddress(const PAddr addr);

u8 Read8(VAddr addr);
u16 Read16(VAddr addr);
u32 Read32(VAddr addr);
u64 Read64(VAddr addr);

void Write8(VAddr addr, u8 data);
void Write16(VAddr addr, u16 data);
void Write32(VAddr addr, u32 data);
void Write64(VAddr addr, u64 data);

void ReadBlock(const Kernel::Process& process, const VAddr src_addr, void* dest_buffer,
               size_t size);
void ReadBlock(const VAddr src_addr, void* dest_buffer, size_t size);
void WriteBlock(const Kernel::Process& process, const VAddr dest_addr, const void* src_buffer,
                size_t size);
void WriteBlock(const VAddr dest_addr, const void* src_buffer, size_t size);
void ZeroBlock(const VAddr dest_addr, const size_t size);
void CopyBlock(VAddr dest_addr, VAddr src_addr, size_t size);

u8* GetPointer(VAddr virtual_address);

std::string ReadCString(VAddr virtual_address, std::size_t max_length);

/**
 * Converts a virtual address inside a region with 1:1 mapping to physical memory to a physical
 * address. This should be used by services to translate addresses for use by the hardware.
 */
boost::optional<PAddr> TryVirtualToPhysicalAddress(VAddr addr);

/**
 * Converts a virtual address inside a region with 1:1 mapping to physical memory to a physical
 * address. This should be used by services to translate addresses for use by the hardware.
 *
 * @deprecated Use TryVirtualToPhysicalAddress(), which reports failure.
 */
PAddr VirtualToPhysicalAddress(VAddr addr);

/**
 * Undoes a mapping performed by VirtualToPhysicalAddress().
 */
boost::optional<VAddr> PhysicalToVirtualAddress(PAddr addr);

/**
 * Gets a pointer to the memory region beginning at the specified physical address.
 */
u8* GetPhysicalPointer(PAddr address);

enum class FlushMode {
    /// Write back modified surfaces to RAM
    Flush,
    /// Remove region from the cache
    Invalidate,
    /// Write back modified surfaces to RAM, and also remove them from the cache
    FlushAndInvalidate,
};

/**
 * Flushes and invalidates any externally cached rasterizer resources touching the given virtual
 * address region.
 */
void RasterizerFlushVirtualRegion(VAddr start, u64 size, FlushMode mode);

} // namespace Memory
