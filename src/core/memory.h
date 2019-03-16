// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>
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
constexpr std::size_t PAGE_BITS = 12;
constexpr u64 PAGE_SIZE = 1ULL << PAGE_BITS;
constexpr u64 PAGE_MASK = PAGE_SIZE - 1;

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
    explicit PageTable();
    explicit PageTable(std::size_t address_space_width_in_bits);
    ~PageTable();

    /**
     * Resizes the page table to be able to accomodate enough pages within
     * a given address space.
     *
     * @param address_space_width_in_bits The address size width in bits.
     */
    void Resize(std::size_t address_space_width_in_bits);

    /**
     * Vector of memory pointers backing each page. An entry can only be non-null if the
     * corresponding entry in the `attributes` vector is of type `Memory`.
     */
    std::vector<u8*> pointers;

    /**
     * Contains MMIO handlers that back memory regions whose entries in the `attribute` vector is
     * of type `Special`.
     */
    boost::icl::interval_map<VAddr, std::set<SpecialRegion>> special_regions;

    /**
     * Vector of fine grained page attributes. If it is set to any value other than `Memory`, then
     * the corresponding entry in `pointers` MUST be set to null.
     */
    std::vector<PageType> attributes;
};

/// Virtual user-space memory regions
enum : VAddr {
    /// Read-only page containing kernel and system configuration values.
    CONFIG_MEMORY_VADDR = 0x1FF80000,
    CONFIG_MEMORY_SIZE = 0x00001000,
    CONFIG_MEMORY_VADDR_END = CONFIG_MEMORY_VADDR + CONFIG_MEMORY_SIZE,

    /// Usually read-only page containing mostly values read from hardware.
    SHARED_PAGE_VADDR = 0x1FF81000,
    SHARED_PAGE_SIZE = 0x00001000,
    SHARED_PAGE_VADDR_END = SHARED_PAGE_VADDR + SHARED_PAGE_SIZE,

    /// TLS (Thread-Local Storage) related.
    TLS_ENTRY_SIZE = 0x200,

    /// Application stack
    DEFAULT_STACK_SIZE = 0x100000,

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

void ReadBlock(const Kernel::Process& process, VAddr src_addr, void* dest_buffer, std::size_t size);
void ReadBlock(VAddr src_addr, void* dest_buffer, std::size_t size);
void WriteBlock(const Kernel::Process& process, VAddr dest_addr, const void* src_buffer,
                std::size_t size);
void WriteBlock(VAddr dest_addr, const void* src_buffer, std::size_t size);
void ZeroBlock(const Kernel::Process& process, VAddr dest_addr, std::size_t size);
void CopyBlock(VAddr dest_addr, VAddr src_addr, std::size_t size);

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

} // namespace Memory
