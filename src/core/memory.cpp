// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>
#include <boost/optional.hpp>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/memory_setup.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Memory {

static std::array<u8, Memory::VRAM_SIZE> vram;

static PageTable* current_page_table = nullptr;

void SetCurrentPageTable(PageTable* page_table) {
    current_page_table = page_table;
    if (Core::System::GetInstance().IsPoweredOn()) {
        Core::CPU().PageTableChanged();
    }
}

PageTable* GetCurrentPageTable() {
    return current_page_table;
}

static void MapPages(PageTable& page_table, VAddr base, u64 size, u8* memory, PageType type) {
    LOG_DEBUG(HW_Memory, "Mapping %p onto %016" PRIX64 "-%016" PRIX64, memory, base * PAGE_SIZE,
              (base + size) * PAGE_SIZE);

    RasterizerFlushVirtualRegion(base << PAGE_BITS, size * PAGE_SIZE,
                                 FlushMode::FlushAndInvalidate);

    VAddr end = base + size;
    while (base != end) {
        ASSERT_MSG(base < PAGE_TABLE_NUM_ENTRIES, "out of range mapping at %016" PRIX64, base);

        page_table.attributes[base] = type;
        page_table.pointers[base] = memory;

        base += 1;
        if (memory != nullptr)
            memory += PAGE_SIZE;
    }
}

void MapMemoryRegion(PageTable& page_table, VAddr base, u64 size, u8* target) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: %016" PRIX64, size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: %016" PRIX64, base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, target, PageType::Memory);
}

void MapIoRegion(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer mmio_handler) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: %016" PRIX64, size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: %016" PRIX64, base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, nullptr, PageType::Special);

    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::IODevice, mmio_handler};
    page_table.special_regions.add(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

void UnmapRegion(PageTable& page_table, VAddr base, u64 size) {
    ASSERT_MSG((size & PAGE_MASK) == 0, "non-page aligned size: %016" PRIX64, size);
    ASSERT_MSG((base & PAGE_MASK) == 0, "non-page aligned base: %016" PRIX64, base);
    MapPages(page_table, base / PAGE_SIZE, size / PAGE_SIZE, nullptr, PageType::Unmapped);

    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    page_table.special_regions.erase(interval);
}

void AddDebugHook(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer hook) {
    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::DebugHook, hook};
    page_table.special_regions.add(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

void RemoveDebugHook(PageTable& page_table, VAddr base, u64 size, MemoryHookPointer hook) {
    auto interval = boost::icl::discrete_interval<VAddr>::closed(base, base + size - 1);
    SpecialRegion region{SpecialRegion::Type::DebugHook, hook};
    page_table.special_regions.subtract(std::make_pair(interval, std::set<SpecialRegion>{region}));
}

/**
 * This function should only be called for virtual addreses with attribute `PageType::Special`.
 */
static std::set<MemoryHookPointer> GetSpecialHandlers(const PageTable& page_table, VAddr vaddr,
                                                      u64 size) {
    std::set<MemoryHookPointer> result;
    auto interval = boost::icl::discrete_interval<VAddr>::closed(vaddr, vaddr + size - 1);
    auto interval_list = page_table.special_regions.equal_range(interval);
    for (auto it = interval_list.first; it != interval_list.second; ++it) {
        for (const auto& region : it->second) {
            result.insert(region.handler);
        }
    }
    return result;
}

static std::set<MemoryHookPointer> GetSpecialHandlers(VAddr vaddr, u64 size) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    return GetSpecialHandlers(page_table, vaddr, size);
}

template <typename T>
boost::optional<T> ReadSpecial(VAddr addr);

template <typename T>
T Read(const VAddr vaddr) {
    if ((vaddr >> PAGE_BITS) >= PAGE_TABLE_NUM_ENTRIES) {
        LOG_ERROR(HW_Memory, "Read%lu after page table @ 0x%016" PRIX64, sizeof(T) * 8, vaddr);
        return 0;
    }

    const PageType type = current_page_table->attributes[vaddr >> PAGE_BITS];
    switch (type) {
    case PageType::Unmapped:
        LOG_ERROR(HW_Memory, "unmapped Read%zu @ 0x%016" PRIX64, sizeof(T) * 8, vaddr);
        return 0;
    case PageType::Special: {
        if (auto result = ReadSpecial<T>(vaddr))
            return *result;
        [[fallthrough]];
    }
    case PageType::Memory: {
        const u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
        ASSERT_MSG(page_pointer, "Mapped memory page without a pointer @ %016" PRIX64, vaddr);

        T value;
        std::memcpy(&value, &page_pointer[vaddr & PAGE_MASK], sizeof(T));
        return value;
    }
    }
    UNREACHABLE();
    return 0;
}

template <typename T>
bool WriteSpecial(VAddr addr, const T data);

template <typename T>
void Write(const VAddr vaddr, const T data) {
    if ((vaddr >> PAGE_BITS) >= PAGE_TABLE_NUM_ENTRIES) {
        LOG_ERROR(HW_Memory, "Write%lu after page table 0x%08X @ 0x%016" PRIX64, sizeof(data) * 8,
                  (u32)data, vaddr);
        return;
    }

    const PageType type = current_page_table->attributes[vaddr >> PAGE_BITS];
    switch (type) {
    case PageType::Unmapped:
        LOG_ERROR(HW_Memory, "unmapped Write%zu 0x%08X @ 0x%016" PRIX64, sizeof(data) * 8,
                  static_cast<u32>(data), vaddr);
        return;
    case PageType::Special: {
        if (WriteSpecial<T>(vaddr, data))
            return;
        [[fallthrough]];
    }
    case PageType::Memory: {
        u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
        ASSERT_MSG(page_pointer, "Mapped memory page without a pointer @ %016" PRIX64, vaddr);
        std::memcpy(&page_pointer[vaddr & PAGE_MASK], &data, sizeof(T));
        return;
    }
    }
    UNREACHABLE();
}

bool IsValidVirtualAddress(const Kernel::Process& process, const VAddr vaddr) {
    auto& page_table = process.vm_manager.page_table;

    if ((vaddr >> PAGE_BITS) >= PAGE_TABLE_NUM_ENTRIES)
        return false;

    const PageType type = current_page_table->attributes[vaddr >> PAGE_BITS];
    switch (type) {
    case PageType::Unmapped:
        return false;
    case PageType::Memory:
        return true;
    case PageType::Special: {
        for (auto handler : GetSpecialHandlers(page_table, vaddr, 1))
            if (auto result = handler->IsValidAddress(vaddr))
                return *result;
        return current_page_table->pointers[vaddr >> PAGE_BITS] != nullptr;
    }
    }
    UNREACHABLE();
    return false;
}

bool IsValidVirtualAddress(const VAddr vaddr) {
    return IsValidVirtualAddress(*Core::CurrentProcess(), vaddr);
}

bool IsValidPhysicalAddress(const PAddr paddr) {
    return GetPhysicalPointer(paddr) != nullptr;
}

u8* GetPointer(const VAddr vaddr) {
    u8* page_pointer = current_page_table->pointers[vaddr >> PAGE_BITS];
    if (page_pointer) {
        return page_pointer + (vaddr & PAGE_MASK);
    }

    LOG_ERROR(HW_Memory, "unknown GetPointer @ 0x%016" PRIx64, vaddr);
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

u8* GetPhysicalPointer(PAddr address) {
    struct MemoryArea {
        PAddr paddr_base;
        u32 size;
    };

    static constexpr MemoryArea memory_areas[] = {
        {VRAM_PADDR, VRAM_SIZE},
        {IO_AREA_PADDR, IO_AREA_SIZE},
        {DSP_RAM_PADDR, DSP_RAM_SIZE},
        {FCRAM_PADDR, FCRAM_N3DS_SIZE},
    };

    const auto area =
        std::find_if(std::begin(memory_areas), std::end(memory_areas), [&](const auto& area) {
            return address >= area.paddr_base && address < area.paddr_base + area.size;
        });

    if (area == std::end(memory_areas)) {
        LOG_ERROR(HW_Memory, "unknown GetPhysicalPointer @ 0x%016" PRIX64, address);
        return nullptr;
    }

    if (area->paddr_base == IO_AREA_PADDR) {
        LOG_ERROR(HW_Memory, "MMIO mappings are not supported yet. phys_addr=0x%016" PRIX64,
                  address);
        return nullptr;
    }

    u64 offset_into_region = address - area->paddr_base;

    u8* target_pointer = nullptr;
    switch (area->paddr_base) {
    case VRAM_PADDR:
        target_pointer = vram.data() + offset_into_region;
        break;
    case DSP_RAM_PADDR:
        break;
    case FCRAM_PADDR:
        for (const auto& region : Kernel::memory_regions) {
            if (offset_into_region >= region.base &&
                offset_into_region < region.base + region.size) {
                target_pointer =
                    region.linear_heap_memory->data() + offset_into_region - region.base;
                break;
            }
        }
        ASSERT_MSG(target_pointer != nullptr, "Invalid FCRAM address");
        break;
    default:
        UNREACHABLE();
    }

    return target_pointer;
}

void RasterizerFlushVirtualRegion(VAddr start, u64 size, FlushMode mode) {
    // Since pages are unmapped on shutdown after video core is shutdown, the renderer may be
    // null here
    if (VideoCore::g_renderer == nullptr) {
        return;
    }

    VAddr end = start + size;

    auto CheckRegion = [&](VAddr region_start, VAddr region_end) {
        if (start >= region_end || end <= region_start) {
            // No overlap with region
            return;
        }

        VAddr overlap_start = std::max(start, region_start);
        VAddr overlap_end = std::min(end, region_end);
        u64 overlap_size = overlap_end - overlap_start;

        auto* rasterizer = VideoCore::g_renderer->Rasterizer();
        switch (mode) {
        case FlushMode::Flush:
            rasterizer->FlushRegion(overlap_start, overlap_size);
            break;
        case FlushMode::Invalidate:
            rasterizer->InvalidateRegion(overlap_start, overlap_size);
            break;
        case FlushMode::FlushAndInvalidate:
            rasterizer->FlushAndInvalidateRegion(overlap_start, overlap_size);
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

static bool ReadSpecialBlock(const Kernel::Process& process, const VAddr src_addr,
                             void* dest_buffer, const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, src_addr, size)) {
        if (handler->ReadBlock(src_addr, dest_buffer, size)) {
            return true;
        }
    }
    return false;
}

void ReadBlock(const Kernel::Process& process, const VAddr src_addr, void* dest_buffer,
               const size_t size) {
    auto& page_table = process.vm_manager.page_table;

    size_t remaining_size = size;
    size_t page_index = src_addr >> PAGE_BITS;
    size_t page_offset = src_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount = std::min<size_t>(PAGE_SIZE - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped:
            LOG_ERROR(HW_Memory,
                      "unmapped ReadBlock @ 0x%016" PRIX64 " (start address = 0x%" PRIx64
                      ", size = %zu)",
                      current_vaddr, src_addr, size);
            std::memset(dest_buffer, 0, copy_amount);
            break;
        case PageType::Special: {
            if (ReadSpecialBlock(process, current_vaddr, dest_buffer, copy_amount))
                break;
            [[fallthrough]];
        }
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);

            const u8* src_ptr = page_table.pointers[page_index] + page_offset;
            std::memcpy(dest_buffer, src_ptr, copy_amount);
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

static bool WriteSpecialBlock(const Kernel::Process& process, const VAddr dest_addr,
                              const void* src_buffer, const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, dest_addr, size)) {
        if (handler->WriteBlock(dest_addr, src_buffer, size)) {
            return true;
        }
    }
    return false;
}

void WriteBlock(const Kernel::Process& process, const VAddr dest_addr, const void* src_buffer,
                const size_t size) {
    auto& page_table = process.vm_manager.page_table;
    size_t remaining_size = size;
    size_t page_index = dest_addr >> PAGE_BITS;
    size_t page_offset = dest_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount = std::min<size_t>(PAGE_SIZE - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (page_table.attributes[page_index]) {
        case PageType::Unmapped:
            LOG_ERROR(HW_Memory,
                      "unmapped WriteBlock @ 0x%016" PRIX64 " (start address = 0x%016" PRIX64
                      ", size = %zu)",
                      current_vaddr, dest_addr, size);
            break;
        case PageType::Special:
            if (WriteSpecialBlock(process, current_vaddr, src_buffer, copy_amount))
                break;
            [[fallthrough]];
        case PageType::Memory: {
            DEBUG_ASSERT(page_table.pointers[page_index]);

            u8* dest_ptr = page_table.pointers[page_index] + page_offset;
            std::memcpy(dest_ptr, src_buffer, copy_amount);
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

void ZeroBlock(const VAddr dest_addr, const size_t size) {
    const auto& process = *Core::CurrentProcess();

    size_t remaining_size = size;
    size_t page_index = dest_addr >> PAGE_BITS;
    size_t page_offset = dest_addr & PAGE_MASK;

    static const std::array<u8, PAGE_SIZE> zeros = {};

    while (remaining_size > 0) {
        const size_t copy_amount = std::min<size_t>(PAGE_SIZE - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (current_page_table->attributes[page_index]) {
        case PageType::Unmapped:
            LOG_ERROR(HW_Memory,
                      "unmapped ZeroBlock @ 0x%016" PRIX64 " (start address = 0x%016" PRIX64
                      ", size = %zu)",
                      current_vaddr, dest_addr, size);
            break;
        case PageType::Special:
            if (WriteSpecialBlock(process, current_vaddr, zeros.data(), copy_amount))
                break;
            [[fallthrough]];
        case PageType::Memory: {
            DEBUG_ASSERT(current_page_table->pointers[page_index]);

            u8* dest_ptr = current_page_table->pointers[page_index] + page_offset;
            std::memset(dest_ptr, 0, copy_amount);
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

void CopyBlock(VAddr dest_addr, VAddr src_addr, const size_t size) {
    const auto& process = *Core::CurrentProcess();

    size_t remaining_size = size;
    size_t page_index = src_addr >> PAGE_BITS;
    size_t page_offset = src_addr & PAGE_MASK;

    while (remaining_size > 0) {
        const size_t copy_amount = std::min<size_t>(PAGE_SIZE - page_offset, remaining_size);
        const VAddr current_vaddr = static_cast<VAddr>((page_index << PAGE_BITS) + page_offset);

        switch (current_page_table->attributes[page_index]) {
        case PageType::Unmapped:
            LOG_ERROR(HW_Memory,
                      "unmapped CopyBlock @ 0x%016" PRIX64 " (start address = 0x%016" PRIX64
                      ", size = %zu)",
                      current_vaddr, src_addr, size);
            ZeroBlock(dest_addr, copy_amount);
            break;
        case PageType::Special: {
            std::vector<u8> buffer(copy_amount);
            if (ReadSpecialBlock(process, current_vaddr, buffer.data(), buffer.size())) {
                WriteBlock(dest_addr, buffer.data(), buffer.size());
                break;
            }
            [[fallthrough]];
        }
        case PageType::Memory: {
            DEBUG_ASSERT(current_page_table->pointers[page_index]);
            const u8* src_ptr = current_page_table->pointers[page_index] + page_offset;
            WriteBlock(dest_addr, src_ptr, copy_amount);
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

template <>
boost::optional<u8> ReadSpecial<u8>(VAddr addr) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u8)))
        if (auto result = handler->Read8(addr))
            return *result;
    return {};
}

template <>
boost::optional<u16> ReadSpecial<u16>(VAddr addr) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u16)))
        if (auto result = handler->Read16(addr))
            return *result;
    return {};
}

template <>
boost::optional<u32> ReadSpecial<u32>(VAddr addr) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u32)))
        if (auto result = handler->Read32(addr))
            return *result;
    return {};
}

template <>
boost::optional<u64> ReadSpecial<u64>(VAddr addr) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u64)))
        if (auto result = handler->Read64(addr))
            return *result;
    return {};
}

template <>
bool WriteSpecial<u8>(VAddr addr, const u8 data) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u8)))
        if (handler->Write8(addr, data))
            return true;
    return false;
}

template <>
bool WriteSpecial<u16>(VAddr addr, const u16 data) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u16)))
        if (handler->Write16(addr, data))
            return true;
    return false;
}

template <>
bool WriteSpecial<u32>(VAddr addr, const u32 data) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u32)))
        if (handler->Write32(addr, data))
            return true;
    return false;
}

template <>
bool WriteSpecial<u64>(VAddr addr, const u64 data) {
    const PageTable& page_table = Core::CurrentProcess()->vm_manager.page_table;
    for (const auto& handler : GetSpecialHandlers(page_table, addr, sizeof(u64)))
        if (handler->Write64(addr, data))
            return true;
    return false;
}

boost::optional<PAddr> TryVirtualToPhysicalAddress(const VAddr addr) {
    if (addr == 0) {
        return 0;
    } else if (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) {
        return addr - VRAM_VADDR + VRAM_PADDR;
    } else if (addr >= LINEAR_HEAP_VADDR && addr < LINEAR_HEAP_VADDR_END) {
        return addr - LINEAR_HEAP_VADDR + FCRAM_PADDR;
    } else if (addr >= NEW_LINEAR_HEAP_VADDR && addr < NEW_LINEAR_HEAP_VADDR_END) {
        return addr - NEW_LINEAR_HEAP_VADDR + FCRAM_PADDR;
    } else if (addr >= DSP_RAM_VADDR && addr < DSP_RAM_VADDR_END) {
        return addr - DSP_RAM_VADDR + DSP_RAM_PADDR;
    } else if (addr >= IO_AREA_VADDR && addr < IO_AREA_VADDR_END) {
        return addr - IO_AREA_VADDR + IO_AREA_PADDR;
    }

    return boost::none;
}

PAddr VirtualToPhysicalAddress(const VAddr addr) {
    auto paddr = TryVirtualToPhysicalAddress(addr);
    if (!paddr) {
        LOG_ERROR(HW_Memory, "Unknown virtual address @ 0x%016" PRIX64, addr);
        // To help with debugging, set bit on address so that it's obviously invalid.
        return addr | 0x80000000;
    }
    return *paddr;
}

boost::optional<VAddr> PhysicalToVirtualAddress(const PAddr addr) {
    if (addr == 0) {
        return 0;
    } else if (addr >= VRAM_PADDR && addr < VRAM_PADDR_END) {
        return addr - VRAM_PADDR + VRAM_VADDR;
    } else if (addr >= FCRAM_PADDR && addr < FCRAM_PADDR_END) {
        return addr - FCRAM_PADDR + Core::CurrentProcess()->GetLinearHeapAreaAddress();
    } else if (addr >= DSP_RAM_PADDR && addr < DSP_RAM_PADDR_END) {
        return addr - DSP_RAM_PADDR + DSP_RAM_VADDR;
    } else if (addr >= IO_AREA_PADDR && addr < IO_AREA_PADDR_END) {
        return addr - IO_AREA_PADDR + IO_AREA_VADDR;
    }

    return boost::none;
}

} // namespace Memory
