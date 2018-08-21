// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include <boost/optional.hpp>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/hle/lock.h"
#include "core/memory.h"
#include "core/memory_setup.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Memory {

static PageTable* current_page_table = nullptr;

void SetCurrentPageTable(PageTable* page_table) {
    current_page_table = page_table;

    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        system.ArmInterface(0).PageTableChanged();
        system.ArmInterface(1).PageTableChanged();
        system.ArmInterface(2).PageTableChanged();
        system.ArmInterface(3).PageTableChanged();
    }
}

PageTable* GetCurrentPageTable() {
    return current_page_table;
}

static void MapPages(PageTable& page_table, VAddr base, u64 size, u8* memory, PageType type) {
    LOG_DEBUG(HW_Memory, "Mapping {} onto {:016X}-{:016X}", fmt::ptr(memory), base * PAGE_SIZE,
              (base + size) * PAGE_SIZE);

    RasterizerFlushVirtualRegion(base << PAGE_BITS, size * PAGE_SIZE,
                                 FlushMode::FlushAndInvalidate);

    VAddr end = base + size;
    while (base != end) {
        ASSERT_MSG(base < PAGE_TABLE_NUM_ENTRIES, "out of range mapping at {:016X}", base);

        page_table.attributes[base] = type;
        page_table.pointers[base] = memory;

        base += 1;
        if (memory != nullptr)
            memory += PAGE_SIZE;
    }
}

void MapMemoryRegion(PageTable& page_table, VAddr base, u64 size, u8* target) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: {:016X}", size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: {:016X}", base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, target, PageType::Memory);
}

void MapIoRegion(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer mmio_handler) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: {:016X}", size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: {:016X}", base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, nullptr, PageType::Special);

    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::IODevice, std::move(mmio_handler)};
    page_table.special_regions.add(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

void UnmapRegion(PageTable& page_table, VAddr base, u64 size) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: {:016X}", size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: {:016X}", base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, nullptr, PageType::Unmapped);

    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    page_table.special_regions.erase(interval);
}

void AddDebugHook(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer hook) {
    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::DebugHook, std::move(hook)};
    page_table.special_regions.add(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

void RemoveDebugHook(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer hook) {
    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::DebugHook, std::move(hook)};
    page_table.special_regions.subtract(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

/**
 * Gets a pointer to the exact memory at the virtual address (i.e. not page aligned)
 * using a VMA from the current process
 */
static u8* GetPointerFromVMA(const Kernel::Process& process, VAddr vaddr) {
    u8* direct_pointer = nullptr;

    auto& vm_manager = process.vm_manager;

    auto it = vm_manager.FindVMA(vaddr);
    ASSERT(it != vm_manager.vma_map.end());

    auto& vma = it->second;
    switch (vma.type) {
    case Kernel::VMAType::AllocatedMemoryBlock:
        direct_pointer = vma.backing_block->data() + vma.offset;
        break;
    case Kernel::VMAType::BackingMemory:
        direct_pointer = vma.backing_memory;
        break;
    case Kernel::VMAType::Free:
        return nullptr;
    default:
        UNREACHABLE();
    }

    return direct_pointer + (vaddr - vma.base);
}

/**
 * Gets a pointer to the exact memory at the virtual address (i.e. not page aligned)
 * using a VMA from the current process.
 */
static u8* GetPointerFromVMA(VAddr vaddr) {
    return GetPointerFromVMA(*Core::CurrentProcess(), vaddr);
}

template <typename T>
T Read(const VAddr vaddr) {
    const u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
    if (page_pointer) {
        // NOTE: Avoid adding any extra logic to this fast-path block
        T value;
        std::memcpy(&value, &page_pointer[vaddr & PAGE_MASK], sizeof(T));
        return value;
    }

    // The memory access might do an MMIO or cached access, so we have to lock the HLE kernel state
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);

    PageType type = current_page_table->attributes[vaddr >> PAGE_BITS];
    switch (type) {
    case PageType::Unmapped:
        LOG_ERROR(HW_Memory, "Unmapped Read{} @ 0x{:08X}", sizeof(T) * 8, vaddr);
        return 0;
    case PageType::Memory:
        ASSERT_MSG(false, "Mapped memory page without a pointer @ {:016X}", vaddr);
        break;
    case PageType::RasterizerCachedMemory: {
        RasterizerFlushVirtualRegion(vaddr, sizeof(T), FlushMode::Flush);

        T value;
        std::memcpy(&value, GetPointerFromVMA(vaddr), sizeof(T));
        return value;
    }
    default:
        UNREACHABLE();
    }
}

template <typename T>
void Write(const VAddr vaddr, const T data) {
    u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
    if (page_pointer) {
        // NOTE: Avoid adding any extra logic to this fast-path block
        std::memcpy(&page_pointer[vaddr & PAGE_MASK], &data, sizeof(T));
        return;
    }

    // The memory access might do an MMIO or cached access, so we have to lock the HLE kernel state
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);

    PageType type = current_page_table->attributes[vaddr >> PAGE_BITS];
    switch (type) {
    case PageType::Unmapped:
        LOG_ERROR(HW_Memory, "Unmapped Write{} 0x{:08X} @ 0x{:016X}", sizeof(data) * 8,
                  static_cast<u32>(data), vaddr);
        return;
    case PageType::Memory:
        ASSERT_MSG(false, "Mapped memory page without a pointer @ {:016X}", vaddr);
        break;
    case PageType::RasterizerCachedMemory: {
        RasterizerFlushVirtualRegion(vaddr, sizeof(T), FlushMode::Invalidate);
        std::memcpy(GetPointerFromVMA(vaddr), &data, sizeof(T));
        break;
    }
    default:
        UNREACHABLE();
    }
}

bool IsValidVirtualAddress(const Kernel::Process& process, const VAddr vaddr) {
    auto& page_table = process.vm_manager.page_table;

    const u8* page_pointer = page_table.pointers[vaddr >> PAGE_BITS];
    if (page_pointer)
        return true;

    if (page_table.attributes[vaddr >> PAGE_BITS] == PageType::RasterizerCachedMemory)
        return true;

    if (page_table.attributes[vaddr >> PAGE_BITS] != PageType::Special)
        return false;

    return false;
}

bool IsValidVirtualAddress(const VAddr vaddr) {
    return IsValidVirtualAddress(*Core::CurrentProcess(), vaddr);
}

bool IsKernelVirtualAddress(const VAddr vaddr) {
    return KERNEL_REGION_VADDR <= vaddr && vaddr < KERNEL_REGION_END;
}

u8* GetPointer(const VAddr vaddr) {
    u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
    if (page_pointer) {
        return page_pointer + (vaddr & PAGE_MASK);
    }

    if (current_page_table->attributes[vaddr >> PAGE_BITS] == PageType::RasterizerCachedMemory) {
        return GetPointerFromVMA(vaddr);
    }

    LOG_ERROR(HW_Memory, "Unknown GetPointer @ 0x{:016X}", vaddr);
    return nullptr;
}

std::string ReadCString(VAddr vaddr, std::size_t max_length) {
    std::string string;
    string.reserve(max_length);
    for (std::size_t i = 0; i < max_length; ++i) {
        char c = Read8(vaddr);
        if (c == '\0')
            break;
        string.push_back(c);
        ++vaddr;
    }
    string.shrink_to_fit();
    return string;
}

void RasterizerMarkRegionCached(VAddr vaddr, u64 size, bool cached) {
    if (vaddr == 0) {
        return;
    }

    // Iterate over a contiguous CPU address space, which corresponds to the specified GPU address
    // space, marking the region as un/cached. The region is marked un/cached at a granularity of
    // CPU pages, hence why we iterate on a CPU page basis (note: GPU page size is different). This
    // assumes the specified GPU address region is contiguous as well.

    u64 num_pages = ((vaddr + size - 1) >> PAGE_BITS) - (vaddr >> PAGE_BITS) + 1;
    for (unsigned i = 0; i < num_pages; ++i, vaddr += PAGE_SIZE) {
        PageType& page_type = current_page_table->attributes[vaddr >> PAGE_BITS];

        if (cached) {
            // Switch page type to cached if now cached
            switch (page_type) {
            case PageType::Unmapped:
                // It is not necessary for a process to have this region mapped into its address
                // space, for example, a system module need not have a VRAM mapping.
                break;
            case PageType::Memory:
                page_type = PageType::RasterizerCachedMemory;
                current_page_table->pointers[vaddr >> PAGE_BITS] = nullptr;
                break;
            case PageType::RasterizerCachedMemory:
                // There can be more than one GPU region mapped per CPU region, so it's common that
                // this area is already marked as cached.
                break;
            default:
                UNREACHABLE();
            }
        } else {
            // Switch page type to uncached if now uncached
            switch (page_type) {
            case PageType::Unmapped:
                // It is not necessary for a process to have this region mapped into its address
                // space, for example, a system module need not have a VRAM mapping.
                break;
            case PageType::Memory:
                // There can be more than one GPU region mapped per CPU region, so it's common that
                // this area is already unmarked as cached.
                break;
            case PageType::RasterizerCachedMemory: {
                u8* pointer = GetPointerFromVMA(vaddr & ~PAGE_MASK);
                if (pointer == nullptr) {
                    // It's possible that this function has been called while updating the pagetable
                    // after unmapping a VMA. In that case the underlying VMA will no longer exist,
                    // and we should just leave the pagetable entry blank.
                    page_type = PageType::Unmapped;
                } else {
                    page_type = PageType::Memory;
                    current_page_table->pointers[vaddr >> PAGE_BITS] = pointer;
                }
                break;
            }
            default:
                UNREACHABLE();
            }
        }
    }
}

void RasterizerFlushVirtualRegion(VAddr start, u64 size, FlushMode mode) {
    auto& system_instance = Core::System::GetInstance();

    // Since pages are unmapped on shutdown after video core is shutdown, the renderer may be
    // null here
    if (!system_instance.IsPoweredOn()) {
        return;
    }

    VAddr end = start + size;

    const auto CheckRegion = [&](VAddr region_start, VAddr region_end) {
        if (start >= region_end || end <= region_start) {
            // No overlap with region
            return;
        }

        const VAddr overlap_start = std::max(start, region_start);
        const VAddr overlap_end = std::min(end, region_end);
        const u64 overlap_size = overlap_end - overlap_start;

        auto& rasterizer = system_instance.Renderer().Rasterizer();
        switch (mode) {
        case FlushMode::Flush:
            rasterizer.FlushRegion(overlap_start, overlap_size);
            break;
        case FlushMode::Invalidate:
            rasterizer.InvalidateRegion(overlap_start, overlap_size);
            break;
        case FlushMode::FlushAndInvalidate:
            rasterizer.FlushAndInvalidateRegion(overlap_start, overlap_size);
            break;
        }
    };

    CheckRegion(PROCESS_IMAGE_VADDR, PROCESS_IMAGE_VADDR_END);
    CheckRegion(HEAP_VADDR, HEAP_VADDR_END);
}

u8 Read8(const VAddr addr) {
    return Read<u8>(addr);
}

u16 Read16(const VAddr addr) {
    return Read<u16_le>(addr);
}

u32 Read32(const VAddr addr) {
    return Read<u32_le>(addr);
}

u64 Read64(const VAddr addr) {
    return Read<u64_le>(addr);
}

void ReadBlock(const Kernel::Process& process, const VAddr src_addr, void* dest_buffer,
               const size_t size) {
    auto& page_table = process.vm_manager.page_table;

    size_t remaining_size = size;
    size_t page_index = src_addr >> PAGE_BITS;
    size_t page_offset = src_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount =
            std::min(static_cast<size_t>(PAGE_SIZE) - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped: {
            LOG_ERROR(HW_Memory,
                      "Unmapped ReadBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                      current_vaddr, src_addr, size);
            std::memset(dest_buffer, 0, copy_amount);
            break;
        }
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);

            const u8* src_ptr = page_table.pointers[page_index] + page_offset;
            std::memcpy(dest_buffer, src_ptr, copy_amount);
            break;
        }
        case PageType::RasterizerCachedMemory: {
            RasterizerFlushVirtualRegion(current_vaddr, static_cast<u32>(copy_amount),
                                         FlushMode::Flush);
            std::memcpy(dest_buffer, GetPointerFromVMA(process, current_vaddr), copy_amount);
            break;
        }
        default:
            UNREACHABLE();
        }

        page_index++;
        page_offset = 0;
        dest_buffer = static_cast<u8*>(dest_buffer) + copy_amount;
        remaining_size -= copy_amount;
    }
}

void ReadBlock(const VAddr src_addr, void* dest_buffer, const size_t size) {
    ReadBlock(*Core::CurrentProcess(), src_addr, dest_buffer, size);
}

void Write8(const VAddr addr, const u8 data) {
    Write<u8>(addr, data);
}

void Write16(const VAddr addr, const u16 data) {
    Write<u16_le>(addr, data);
}

void Write32(const VAddr addr, const u32 data) {
    Write<u32_le>(addr, data);
}

void Write64(const VAddr addr, const u64 data) {
    Write<u64_le>(addr, data);
}

void WriteBlock(const Kernel::Process& process, const VAddr dest_addr, const void* src_buffer,
                const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    size_t remaining_size = size;
    size_t page_index = dest_addr >> PAGE_BITS;
    size_t page_offset = dest_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount =
            std::min(static_cast<size_t>(PAGE_SIZE) - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped: {
            LOG_ERROR(HW_Memory,
                      "Unmapped WriteBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                      current_vaddr, dest_addr, size);
            break;
        }
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);

            u8* dest_ptr = page_table.pointers[page_index] + page_offset;
            std::memcpy(dest_ptr, src_buffer, copy_amount);
            break;
        }
        case PageType::RasterizerCachedMemory: {
            RasterizerFlushVirtualRegion(current_vaddr, static_cast<u32>(copy_amount),
                                         FlushMode::Invalidate);
            std::memcpy(GetPointerFromVMA(process, current_vaddr), src_buffer, copy_amount);
            break;
        }
        default:
            UNREACHABLE();
        }

        page_index++;
        page_offset = 0;
        src_buffer = static_cast<const u8*>(src_buffer) + copy_amount;
        remaining_size -= copy_amount;
    }
}

void WriteBlock(const VAddr dest_addr, const void* src_buffer, const size_t size) {
    WriteBlock(*Core::CurrentProcess(), dest_addr, src_buffer, size);
}

void ZeroBlock(const Kernel::Process& process, const VAddr dest_addr, const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    size_t remaining_size = size;
    size_t page_index = dest_addr >> PAGE_BITS;
    size_t page_offset = dest_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount =
            std::min(static_cast<size_t>(PAGE_SIZE) - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped: {
            LOG_ERROR(HW_Memory,
                      "Unmapped ZeroBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                      current_vaddr, dest_addr, size);
            break;
        }
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);

            u8* dest_ptr = page_table.pointers[page_index] + page_offset;
            std::memset(dest_ptr, 0, copy_amount);
            break;
        }
        case PageType::RasterizerCachedMemory: {
            RasterizerFlushVirtualRegion(current_vaddr, static_cast<u32>(copy_amount),
                                         FlushMode::Invalidate);
            std::memset(GetPointerFromVMA(process, current_vaddr), 0, copy_amount);
            break;
        }
        default:
            UNREACHABLE();
        }

        page_index++;
        page_offset = 0;
        remaining_size -= copy_amount;
    }
}

void CopyBlock(const Kernel::Process& process, VAddr dest_addr, VAddr src_addr, const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    size_t remaining_size = size;
    size_t page_index = src_addr >> PAGE_BITS;
    size_t page_offset = src_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount =
            std::min(static_cast<size_t>(PAGE_SIZE) - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped: {
            LOG_ERROR(HW_Memory,
                      "Unmapped CopyBlock @ 0x{:016X} (start address = 0x{:016X}, size = {})",
                      current_vaddr, src_addr, size);
            ZeroBlock(process, dest_addr, copy_amount);
            break;
        }
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);
            const u8* src_ptr = page_table.pointers[page_index] + page_offset;
            WriteBlock(process, dest_addr, src_ptr, copy_amount);
            break;
        }
        case PageType::RasterizerCachedMemory: {
            RasterizerFlushVirtualRegion(current_vaddr, static_cast<u32>(copy_amount),
                                         FlushMode::Flush);
            WriteBlock(process, dest_addr, GetPointerFromVMA(process, current_vaddr), copy_amount);
            break;
        }
        default:
            UNREACHABLE();
        }

        page_index++;
        page_offset = 0;
        dest_addr += static_cast<VAddr>(copy_amount);
        src_addr += static_cast<VAddr>(copy_amount);
        remaining_size -= copy_amount;
    }
}

void CopyBlock(VAddr dest_addr, VAddr src_addr, size_t size) {
    CopyBlock(*Core::CurrentProcess(), dest_addr, src_addr, size);
}

} // namespace Memory
