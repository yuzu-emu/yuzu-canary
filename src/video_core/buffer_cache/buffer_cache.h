// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"
#include "core/core.h"
#include "video_core/buffer_cache/buffer_block.h"
#include "video_core/buffer_cache/map_interval.h"
#include "video_core/memory_manager.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace VideoCommon {

using MapInterval = std::shared_ptr<MapIntervalBase>;

template <typename TBuffer, typename TBufferType, typename StreamBuffer>
class BufferCache {
public:
    using BufferInfo = std::pair<const TBufferType*, u64>;

    BufferInfo UploadMemory(GPUVAddr gpu_addr, std::size_t size, std::size_t alignment = 4,
                            bool is_written = false) {
        std::lock_guard lock{mutex};

        auto& memory_manager = system.GPU().MemoryManager();
        const auto host_ptr = memory_manager.GetPointer(gpu_addr);
        if (!host_ptr) {
            return {GetEmptyBuffer(size), 0};
        }
        const auto cache_addr = ToCacheAddr(host_ptr);

        // Cache management is a big overhead, so only cache entries with a given size.
        // TODO: Figure out which size is the best for given games.
        constexpr std::size_t max_stream_size = 0x800;
        if (size < max_stream_size) {
            if (!is_written && !IsRegionWritten(cache_addr, cache_addr + size - 1)) {
                return StreamBufferUpload(host_ptr, size, alignment);
            }
        }

        auto block = GetBlock(cache_addr, size);
        auto map = MapAddress(block, gpu_addr, cache_addr, size);
        if (is_written) {
            map->MarkAsModified(true, GetModifiedTicks());
            if (!map->IsWritten()) {
                map->MarkAsWritten(true);
                MarkRegionAsWritten(map->GetStart(), map->GetEnd() - 1);
            }
        } else {
            if (map->IsWritten()) {
                WriteBarrier();
            }
        }

        const u64 offset = static_cast<u64>(block->GetOffset(cache_addr));

        return {ToHandle(block), offset};
    }

    /// Uploads from a host memory. Returns the OpenGL buffer where it's located and its offset.
    BufferInfo UploadHostMemory(const void* raw_pointer, std::size_t size,
                                std::size_t alignment = 4) {
        std::lock_guard lock{mutex};
        return StreamBufferUpload(raw_pointer, size, alignment);
    }

    void Map(std::size_t max_size) {
        std::lock_guard lock{mutex};

        std::tie(buffer_ptr, buffer_offset_base, invalidated) = stream_buffer->Map(max_size, 4);
        buffer_offset = buffer_offset_base;
    }

    /// Finishes the upload stream, returns true on bindings invalidation.
    bool Unmap() {
        std::lock_guard lock{mutex};

        stream_buffer->Unmap(buffer_offset - buffer_offset_base);
        return std::exchange(invalidated, false);
    }

    void TickFrame() {
        ++epoch;
        while (!pending_destruction.empty()) {
            if (pending_destruction.front()->GetEpoch() + 1 > epoch) {
                break;
            }
            pending_destruction.pop_front();
        }
    }

    /// Write any cached resources overlapping the specified region back to memory
    void FlushRegion(CacheAddr addr, std::size_t size) {
        std::lock_guard lock{mutex};

        std::vector<MapInterval> objects = GetMapsInRange(addr, size);
        std::sort(objects.begin(), objects.end(), [](const MapInterval& a, const MapInterval& b) {
            return a->GetModificationTick() < b->GetModificationTick();
        });
        for (auto& object : objects) {
            if (object->IsModified() && object->IsRegistered()) {
                FlushMap(object);
            }
        }
    }

    /// Mark the specified region as being invalidated
    void InvalidateRegion(CacheAddr addr, u64 size) {
        std::lock_guard lock{mutex};

        std::vector<MapInterval> objects = GetMapsInRange(addr, size);
        for (auto& object : objects) {
            if (object->IsRegistered()) {
                Unregister(object);
            }
        }
    }

    virtual const TBufferType* GetEmptyBuffer(std::size_t size) = 0;

protected:
    explicit BufferCache(VideoCore::RasterizerInterface& rasterizer, Core::System& system,
                         std::unique_ptr<StreamBuffer> stream_buffer)
        : rasterizer{rasterizer}, system{system}, stream_buffer{std::move(stream_buffer)},
          stream_buffer_handle{this->stream_buffer->GetHandle()} {}

    ~BufferCache() = default;

    virtual const TBufferType* ToHandle(const TBuffer& storage) = 0;

    virtual void WriteBarrier() = 0;

    virtual TBuffer CreateBlock(CacheAddr cache_addr, std::size_t size) = 0;

    virtual void UploadBlockData(const TBuffer& buffer, std::size_t offset, std::size_t size,
                                 const u8* data) = 0;

    virtual void DownloadBlockData(const TBuffer& buffer, std::size_t offset, std::size_t size,
                                   u8* data) = 0;

    virtual void CopyBlock(const TBuffer& src, const TBuffer& dst, std::size_t src_offset,
                           std::size_t dst_offset, std::size_t size) = 0;

    /// Register an object into the cache
    void Register(const MapInterval& new_map, bool inherit_written = false) {
        const CacheAddr cache_ptr = new_map->GetStart();
        const std::optional<VAddr> cpu_addr =
            system.GPU().MemoryManager().GpuToCpuAddress(new_map->GetGpuAddress());
        if (!cache_ptr || !cpu_addr) {
            LOG_CRITICAL(HW_GPU, "Failed to register buffer with unmapped gpu_address 0x{:016x}",
                         new_map->GetGpuAddress());
            return;
        }
        const std::size_t size = new_map->GetEnd() - new_map->GetStart();
        new_map->SetCpuAddress(*cpu_addr);
        new_map->MarkAsRegistered(true);
        const IntervalType interval{new_map->GetStart(), new_map->GetEnd()};
        mapped_addresses.insert({interval, new_map});
        rasterizer.UpdatePagesCachedCount(*cpu_addr, size, 1);
        if (inherit_written) {
            MarkRegionAsWritten(new_map->GetStart(), new_map->GetEnd() - 1);
            new_map->MarkAsWritten(true);
        }
    }

    /// Unregisters an object from the cache
    void Unregister(MapInterval& map) {
        const std::size_t size = map->GetEnd() - map->GetStart();
        rasterizer.UpdatePagesCachedCount(map->GetCpuAddress(), size, -1);
        map->MarkAsRegistered(false);
        if (map->IsWritten()) {
            UnmarkRegionAsWritten(map->GetStart(), map->GetEnd() - 1);
        }
        const IntervalType delete_interval{map->GetStart(), map->GetEnd()};
        mapped_addresses.erase(delete_interval);
    }

private:
    MapInterval CreateMap(const CacheAddr start, const CacheAddr end, const GPUVAddr gpu_addr) {
        return std::make_shared<MapIntervalBase>(start, end, gpu_addr);
    }

    MapInterval MapAddress(const TBuffer& block, const GPUVAddr gpu_addr,
                           const CacheAddr cache_addr, const std::size_t size) {

        std::vector<MapInterval> overlaps = GetMapsInRange(cache_addr, size);
        if (overlaps.empty()) {
            const CacheAddr cache_addr_end = cache_addr + size;
            MapInterval new_map = CreateMap(cache_addr, cache_addr_end, gpu_addr);
            u8* host_ptr = FromCacheAddr(cache_addr);
            UploadBlockData(block, block->GetOffset(cache_addr), size, host_ptr);
            Register(new_map);
            return new_map;
        }

        const CacheAddr cache_addr_end = cache_addr + size;
        if (overlaps.size() == 1) {
            MapInterval& current_map = overlaps[0];
            if (current_map->IsInside(cache_addr, cache_addr_end)) {
                return current_map;
            }
        }
        CacheAddr new_start = cache_addr;
        CacheAddr new_end = cache_addr_end;
        bool write_inheritance = false;
        bool modified_inheritance = false;
        // Calculate new buffer parameters
        for (auto& overlap : overlaps) {
            new_start = std::min(overlap->GetStart(), new_start);
            new_end = std::max(overlap->GetEnd(), new_end);
            write_inheritance |= overlap->IsWritten();
            modified_inheritance |= overlap->IsModified();
        }
        GPUVAddr new_gpu_addr = gpu_addr + new_start - cache_addr;
        for (auto& overlap : overlaps) {
            Unregister(overlap);
        }
        UpdateBlock(block, new_start, new_end, overlaps);
        MapInterval new_map = CreateMap(new_start, new_end, new_gpu_addr);
        if (modified_inheritance) {
            new_map->MarkAsModified(true, GetModifiedTicks());
        }
        Register(new_map, write_inheritance);
        return new_map;
    }

    void UpdateBlock(const TBuffer& block, CacheAddr start, CacheAddr end,
                     std::vector<MapInterval>& overlaps) {
        const IntervalType base_interval{start, end};
        IntervalSet interval_set{};
        interval_set.add(base_interval);
        for (auto& overlap : overlaps) {
            const IntervalType subtract{overlap->GetStart(), overlap->GetEnd()};
            interval_set.subtract(subtract);
        }
        for (auto& interval : interval_set) {
            std::size_t size = interval.upper() - interval.lower();
            if (size > 0) {
                u8* host_ptr = FromCacheAddr(interval.lower());
                UploadBlockData(block, block->GetOffset(interval.lower()), size, host_ptr);
            }
        }
    }

    std::vector<MapInterval> GetMapsInRange(CacheAddr addr, std::size_t size) {
        if (size == 0) {
            return {};
        }

        std::vector<MapInterval> objects{};
        const IntervalType interval{addr, addr + size};
        for (auto& pair : boost::make_iterator_range(mapped_addresses.equal_range(interval))) {
            objects.push_back(pair.second);
        }

        return objects;
    }

    /// Returns a ticks counter used for tracking when cached objects were last modified
    u64 GetModifiedTicks() {
        return ++modified_ticks;
    }

    void FlushMap(MapInterval map) {
        std::size_t size = map->GetEnd() - map->GetStart();
        TBuffer block = blocks[map->GetStart() >> block_page_bits];
        u8* host_ptr = FromCacheAddr(map->GetStart());
        DownloadBlockData(block, block->GetOffset(map->GetStart()), size, host_ptr);
        map->MarkAsModified(false, 0);
    }

    BufferInfo StreamBufferUpload(const void* raw_pointer, std::size_t size,
                                  std::size_t alignment) {
        AlignBuffer(alignment);
        const std::size_t uploaded_offset = buffer_offset;
        std::memcpy(buffer_ptr, raw_pointer, size);

        buffer_ptr += size;
        buffer_offset += size;
        return {&stream_buffer_handle, uploaded_offset};
    }

    void AlignBuffer(std::size_t alignment) {
        // Align the offset, not the mapped pointer
        const std::size_t offset_aligned = Common::AlignUp(buffer_offset, alignment);
        buffer_ptr += offset_aligned - buffer_offset;
        buffer_offset = offset_aligned;
    }

    TBuffer EnlargeBlock(TBuffer buffer) {
        const std::size_t old_size = buffer->GetSize();
        const std::size_t new_size = old_size + block_page_size;
        const CacheAddr cache_addr = buffer->GetCacheAddr();
        TBuffer new_buffer = CreateBlock(cache_addr, new_size);
        CopyBlock(buffer, new_buffer, 0, 0, old_size);
        buffer->SetEpoch(epoch);
        pending_destruction.push_back(buffer);
        const CacheAddr cache_addr_end = cache_addr + new_size - 1;
        u64 page_start = cache_addr >> block_page_bits;
        const u64 page_end = cache_addr_end >> block_page_bits;
        while (page_start <= page_end) {
            blocks[page_start] = new_buffer;
            ++page_start;
        }
        return new_buffer;
    }

    TBuffer MergeBlocks(TBuffer first, TBuffer second) {
        const std::size_t size_1 = first->GetSize();
        const std::size_t size_2 = second->GetSize();
        const CacheAddr first_addr = first->GetCacheAddr();
        const CacheAddr second_addr = second->GetCacheAddr();
        const CacheAddr new_addr = std::min(first_addr, second_addr);
        const std::size_t new_size = size_1 + size_2;
        TBuffer new_buffer = CreateBlock(new_addr, new_size);
        CopyBlock(first, new_buffer, 0, new_buffer->GetOffset(first_addr), size_1);
        CopyBlock(second, new_buffer, 0, new_buffer->GetOffset(second_addr), size_2);
        first->SetEpoch(epoch);
        second->SetEpoch(epoch);
        pending_destruction.push_back(first);
        pending_destruction.push_back(second);
        const CacheAddr cache_addr_end = new_addr + new_size - 1;
        u64 page_start = new_addr >> block_page_bits;
        const u64 page_end = cache_addr_end >> block_page_bits;
        while (page_start <= page_end) {
            blocks[page_start] = new_buffer;
            ++page_start;
        }
        return new_buffer;
    }

    TBuffer GetBlock(const CacheAddr cache_addr, const std::size_t size) {
        TBuffer found{};
        const CacheAddr cache_addr_end = cache_addr + size - 1;
        u64 page_start = cache_addr >> block_page_bits;
        const u64 page_end = cache_addr_end >> block_page_bits;
        const u64 num_pages = page_end - page_start + 1;
        while (page_start <= page_end) {
            auto it = blocks.find(page_start);
            if (it == blocks.end()) {
                if (found) {
                    found = EnlargeBlock(found);
                } else {
                    const CacheAddr start_addr = (page_start << block_page_bits);
                    found = CreateBlock(start_addr, block_page_size);
                    blocks[page_start] = found;
                }
            } else {
                if (found) {
                    if (found == it->second) {
                        ++page_start;
                        continue;
                    }
                    found = MergeBlocks(found, it->second);
                } else {
                    found = it->second;
                }
            }
            ++page_start;
        }
        return found;
    }

    void MarkRegionAsWritten(const CacheAddr start, const CacheAddr end) {
        u64 page_start = start >> write_page_bit;
        const u64 page_end = end >> write_page_bit;
        while (page_start <= page_end) {
            auto it = written_pages.find(page_start);
            if (it != written_pages.end()) {
                it->second = it->second + 1;
            } else {
                written_pages[page_start] = 1;
            }
            page_start++;
        }
    }

    void UnmarkRegionAsWritten(const CacheAddr start, const CacheAddr end) {
        u64 page_start = start >> write_page_bit;
        const u64 page_end = end >> write_page_bit;
        while (page_start <= page_end) {
            auto it = written_pages.find(page_start);
            if (it != written_pages.end()) {
                if (it->second > 1) {
                    it->second = it->second - 1;
                } else {
                    written_pages.erase(it);
                }
            }
            page_start++;
        }
    }

    bool IsRegionWritten(const CacheAddr start, const CacheAddr end) const {
        u64 page_start = start >> write_page_bit;
        const u64 page_end = end >> write_page_bit;
        while (page_start <= page_end) {
            if (written_pages.count(page_start) > 0) {
                return true;
            }
            page_start++;
        }
        return false;
    }

    std::unique_ptr<StreamBuffer> stream_buffer;
    TBufferType stream_buffer_handle{};

    bool invalidated = false;

    u8* buffer_ptr = nullptr;
    u64 buffer_offset = 0;
    u64 buffer_offset_base = 0;

    using IntervalSet = boost::icl::interval_set<CacheAddr>;
    using IntervalCache = boost::icl::interval_map<CacheAddr, MapInterval>;
    using IntervalType = typename IntervalCache::interval_type;
    IntervalCache mapped_addresses{};

    static constexpr u64 write_page_bit{11};
    std::unordered_map<u64, u32> written_pages{};

    static constexpr u64 block_page_bits{21};
    static constexpr u64 block_page_size{1 << block_page_bits};
    std::unordered_map<u64, TBuffer> blocks{};

    std::list<TBuffer> pending_destruction{};
    u64 epoch{};
    u64 modified_ticks{};
    VideoCore::RasterizerInterface& rasterizer;
    Core::System& system;
    std::recursive_mutex mutex;
};

} // namespace VideoCommon
