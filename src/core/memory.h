// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <tuple>
#include <boost/icl/interval_map.hpp>
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

/// Virtual user-space memory regions
enum : VAddr {
    /// Where the application text, data and bss reside.
    PROCESS_IMAGE_VADDR = 0x08000000,
    PROCESS_IMAGE_MAX_SIZE = 0x08000000,
    PROCESS_IMAGE_VADDR_END = PROCESS_IMAGE_VADDR + PROCESS_IMAGE_MAX_SIZE,

    /// Read-only page containing kernel and system configuration values.
    CONFIG_MEMORY_VADDR = 0x1FF80000,
    CONFIG_MEMORY_SIZE = 0x00001000,
    CONFIG_MEMORY_VADDR_END = CONFIG_MEMORY_VADDR + CONFIG_MEMORY_SIZE,

    /// Usually read-only page containing mostly values read from hardware.
    SHARED_PAGE_VADDR = 0x1FF81000,
    SHARED_PAGE_SIZE = 0x00001000,
    SHARED_PAGE_VADDR_END = SHARED_PAGE_VADDR + SHARED_PAGE_SIZE,

    /// Area where TLS (Thread-Local Storage) buffers are allocated.
    TLS_AREA_VADDR = 0x40000000,
    TLS_ENTRY_SIZE = 0x200,
    TLS_AREA_SIZE = 0x10000000,
    TLS_AREA_VADDR_END = TLS_AREA_VADDR + TLS_AREA_SIZE,

    /// Application stack
    STACK_AREA_VADDR = TLS_AREA_VADDR_END,
    STACK_AREA_SIZE = 0x10000000,
    STACK_AREA_VADDR_END = STACK_AREA_VADDR + STACK_AREA_SIZE,
    DEFAULT_STACK_SIZE = 0x100000,

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

    /// Kernel Virtual Address Range
    KERNEL_REGION_VADDR = 0xFFFFFF8000000000,
    KERNEL_REGION_SIZE = 0x7FFFE00000,
    KERNEL_REGION_END = KERNEL_REGION_VADDR + KERNEL_REGION_SIZE,
};

/// Currently active page table
void SetCurrentPageTable(PageTable* page_table);
PageTable* GetCurrentPageTable();

/// Determines if the given VAddr is valid for the specified process.
bool IsValidVirtualAddress(const Kernel::Process& process, VAddr vaddr);
bool IsValidVirtualAddress(VAddr vaddr);
/// Determines if the given VAddr is a kernel address
bool IsKernelVirtualAddress(VAddr vaddr);

u8 Read8(VAddr addr);
u16 Read16(VAddr addr);
u32 Read32(VAddr addr);
u64 Read64(VAddr addr);

void Write8(VAddr addr, u8 data);
void Write16(VAddr addr, u16 data);
void Write32(VAddr addr, u32 data);
void Write64(VAddr addr, u64 data);

void ReadBlock(const Kernel::Process& process, VAddr src_addr, void* dest_buffer, size_t size);
void ReadBlock(VAddr src_addr, void* dest_buffer, size_t size);
void WriteBlock(const Kernel::Process& process, VAddr dest_addr, const void* src_buffer,
                size_t size);
void WriteBlock(VAddr dest_addr, const void* src_buffer, size_t size);
void ZeroBlock(const Kernel::Process& process, VAddr dest_addr, size_t size);
void CopyBlock(VAddr dest_addr, VAddr src_addr, size_t size);

u8* GetPointer(VAddr vaddr);

std::string ReadCString(VAddr vaddr, std::size_t max_length);

enum class FlushMode {
    /// Write back modified surfaces to RAM
    Flush,
    /// Remove region from the cache
    Invalidate,
    /// Write back modified surfaces to RAM, and also remove them from the cache
    FlushAndInvalidate,
};

/**
 * Mark each page touching the region as cached.
 */
void RasterizerMarkRegionCached(VAddr vaddr, u64 size, bool cached);

/**
 * Flushes and invalidates any externally cached rasterizer resources touching the given virtual
 * address region.
 */
void RasterizerFlushVirtualRegion(VAddr start, u64 size, FlushMode mode);

} // namespace Memory
