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
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_cache.h"

namespace VideoCore {
class RasterizerInterface;
}

namespace VideoCommon {

template <typename BufferStorageType>
class CachedBuffer final : public RasterizerCacheObject {
public:
    explicit CachedBuffer(VAddr cpu_addr, u8* host_ptr)
        : RasterizerCacheObject{host_ptr}, host_ptr{host_ptr}, cpu_addr{cpu_addr} {}
    ~CachedBuffer() override = default;

    VAddr GetCpuAddr() const override {
        return cpu_addr;
    }

    std::size_t GetSizeInBytes() const override {
        return size;
    }

    u8* GetWritableHostPtr() const {
        return host_ptr;
    }

    std::size_t GetSize() const {
        return size;
    }

    std::size_t GetCapacity() const {
        return capacity;
    }

    bool IsInternalized() const {
        return is_internal;
    }

    const BufferStorageType& GetBuffer() const {
        return buffer;
    }

    void SetSize(std::size_t new_size) {
        size = new_size;
    }

    void SetInternalState(bool is_internal_) {
        is_internal = is_internal_;
    }

    BufferStorageType ExchangeBuffer(BufferStorageType buffer_, std::size_t new_capacity) {
        capacity = new_capacity;
        std::swap(buffer, buffer_);
        return buffer_;
    }

private:
    u8* host_ptr{};
    VAddr cpu_addr{};
    std::size_t size{};
    std::size_t capacity{};
    bool is_internal{};
    BufferStorageType buffer;
};

template <typename BufferStorageType, typename BufferType, typename StreamBuffer>
class BufferCache : public RasterizerCache<std::shared_ptr<CachedBuffer<BufferStorageType>>> {
public:
    using Buffer = std::shared_ptr<CachedBuffer<BufferStorageType>>;
    using BufferInfo = std::pair<const BufferType*, u64>;

    explicit BufferCache(VideoCore::RasterizerInterface& rasterizer, Core::System& system,
                         std::unique_ptr<StreamBuffer> stream_buffer)
        : RasterizerCache<Buffer>{rasterizer}, system{system},
          stream_buffer{std::move(stream_buffer)}, stream_buffer_handle{
                                                       this->stream_buffer->GetHandle()} {}
    ~BufferCache() = default;

    void Unregister(const Buffer& entry) override {
        std::lock_guard lock{RasterizerCache<Buffer>::mutex};
        if (entry->IsInternalized()) {
            internalized_entries.erase(entry->GetCacheAddr());
        }
        ReserveBuffer(entry);
        RasterizerCache<Buffer>::Unregister(entry);
    }

    void TickFrame() {
        marked_for_destruction_index =
            (marked_for_destruction_index + 1) % marked_for_destruction_ring_buffer.size();
        MarkedForDestruction().clear();
    }

    BufferInfo UploadMemory(GPUVAddr gpu_addr, std::size_t size, std::size_t alignment = 4,
                            bool internalize = false, bool is_written = false) {
        std::lock_guard lock{RasterizerCache<Buffer>::mutex};

        auto& memory_manager = system.GPU().MemoryManager();
        const auto host_ptr = memory_manager.GetPointer(gpu_addr);
        if (!host_ptr) {
            return {GetEmptyBuffer(size), 0};
        }
        const auto cache_addr = ToCacheAddr(host_ptr);

        // Cache management is a big overhead, so only cache entries with a given size.
        // TODO: Figure out which size is the best for given games.
        constexpr std::size_t max_stream_size = 0x800;
        if (!internalize && size < max_stream_size &&
            internalized_entries.find(cache_addr) == internalized_entries.end()) {
            return StreamBufferUpload(host_ptr, size, alignment);
        }

        auto entry = RasterizerCache<Buffer>::TryGet(cache_addr);
        if (!entry) {
            return FixedBufferUpload(gpu_addr, host_ptr, size, internalize, is_written);
        }

        if (entry->GetSize() < size) {
            IncreaseBufferSize(entry, size);
        }
        if (is_written) {
            entry->MarkAsModified(true, *this);
        }
        return {ToHandle(entry->GetBuffer()), 0};
    }

    /// Uploads from a host memory. Returns the OpenGL buffer where it's located and its offset.
    BufferInfo UploadHostMemory(const void* raw_pointer, std::size_t size,
                                std::size_t alignment = 4) {
        std::lock_guard lock{RasterizerCache<Buffer>::mutex};
        return StreamBufferUpload(raw_pointer, size, alignment);
    }

    void Map(std::size_t max_size) {
        std::tie(buffer_ptr, buffer_offset_base, invalidated) = stream_buffer->Map(max_size, 4);
        buffer_offset = buffer_offset_base;
    }

    /// Finishes the upload stream, returns true on bindings invalidation.
    bool Unmap() {
        stream_buffer->Unmap(buffer_offset - buffer_offset_base);
        return std::exchange(invalidated, false);
    }

    virtual const BufferType* GetEmptyBuffer(std::size_t size) = 0;

protected:
    void FlushObjectInner(const Buffer& entry) override {
        DownloadBufferData(entry->GetBuffer(), 0, entry->GetSize(), entry->GetWritableHostPtr());
    }

    virtual BufferStorageType CreateBuffer(std::size_t size) = 0;

    virtual const BufferType* ToHandle(const BufferStorageType& storage) = 0;

    virtual void UploadBufferData(const BufferStorageType& buffer, std::size_t offset,
                                  std::size_t size, const u8* data) = 0;

    virtual void DownloadBufferData(const BufferStorageType& buffer, std::size_t offset,
                                    std::size_t size, u8* data) = 0;

    virtual void CopyBufferData(const BufferStorageType& src, const BufferStorageType& dst,
                                std::size_t src_offset, std::size_t dst_offset,
                                std::size_t size) = 0;

private:
    BufferInfo StreamBufferUpload(const void* raw_pointer, std::size_t size,
                                  std::size_t alignment) {
        AlignBuffer(alignment);
        const std::size_t uploaded_offset = buffer_offset;
        std::memcpy(buffer_ptr, raw_pointer, size);

        buffer_ptr += size;
        buffer_offset += size;
        return {&stream_buffer_handle, uploaded_offset};
    }

    BufferInfo FixedBufferUpload(GPUVAddr gpu_addr, u8* host_ptr, std::size_t size,
                                 bool internalize, bool is_written) {
        auto& memory_manager = Core::System::GetInstance().GPU().MemoryManager();
        const auto cpu_addr = memory_manager.GpuToCpuAddress(gpu_addr);
        ASSERT(cpu_addr);

        auto entry = GetUncachedBuffer(*cpu_addr, host_ptr);
        entry->SetSize(size);
        entry->SetInternalState(internalize);
        RasterizerCache<Buffer>::Register(entry);

        if (internalize) {
            internalized_entries.emplace(ToCacheAddr(host_ptr));
        }
        if (is_written) {
            entry->MarkAsModified(true, *this);
        }

        if (entry->GetCapacity() < size) {
            MarkedForDestruction().push_back(entry->ExchangeBuffer(CreateBuffer(size), size));
        }

        UploadBufferData(entry->GetBuffer(), 0, size, host_ptr);
        return {ToHandle(entry->GetBuffer()), 0};
    }

    void IncreaseBufferSize(Buffer& entry, std::size_t new_size) {
        const std::size_t old_size = entry->GetSize();
        if (entry->GetCapacity() < new_size) {
            const auto& old_buffer = entry->GetBuffer();
            auto new_buffer = CreateBuffer(new_size);

            // Copy bits from the old buffer to the new buffer.
            CopyBufferData(old_buffer, new_buffer, 0, 0, old_size);
            MarkedForDestruction().push_back(
                entry->ExchangeBuffer(std::move(new_buffer), new_size));

            // This buffer could have been used
            invalidated = true;
        }
        // Upload the new bits.
        const std::size_t size_diff = new_size - old_size;
        UploadBufferData(entry->GetBuffer(), old_size, size_diff, entry->GetHostPtr() + old_size);

        // Update entry's size in the object and in the cache.
        Unregister(entry);

        entry->SetSize(new_size);
        RasterizerCache<Buffer>::Register(entry);
    }

    Buffer GetUncachedBuffer(VAddr cpu_addr, u8* host_ptr) {
        if (auto entry = TryGetReservedBuffer(host_ptr)) {
            return entry;
        }
        return std::make_shared<CachedBuffer<BufferStorageType>>(cpu_addr, host_ptr);
    }

    Buffer TryGetReservedBuffer(u8* host_ptr) {
        const auto it = buffer_reserve.find(ToCacheAddr(host_ptr));
        if (it == buffer_reserve.end()) {
            return {};
        }
        auto& reserve = it->second;
        auto entry = reserve.back();
        reserve.pop_back();
        return entry;
    }

    void ReserveBuffer(Buffer entry) {
        buffer_reserve[entry->GetCacheAddr()].push_back(std::move(entry));
    }

    void AlignBuffer(std::size_t alignment) {
        // Align the offset, not the mapped pointer
        const std::size_t offset_aligned = Common::AlignUp(buffer_offset, alignment);
        buffer_ptr += offset_aligned - buffer_offset;
        buffer_offset = offset_aligned;
    }

    std::vector<BufferStorageType>& MarkedForDestruction() {
        return marked_for_destruction_ring_buffer[marked_for_destruction_index];
    }

    Core::System& system;

    std::unique_ptr<StreamBuffer> stream_buffer;
    BufferType stream_buffer_handle{};

    bool invalidated = false;

    u8* buffer_ptr = nullptr;
    u64 buffer_offset = 0;
    u64 buffer_offset_base = 0;

    std::size_t marked_for_destruction_index = 0;
    std::array<std::vector<BufferStorageType>, 4> marked_for_destruction_ring_buffer;

    std::unordered_set<CacheAddr> internalized_entries;
    std::unordered_map<CacheAddr, std::vector<Buffer>> buffer_reserve;
};

} // namespace VideoCommon
