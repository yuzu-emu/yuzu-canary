// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <set>
#include <unordered_map>

#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range_core.hpp>

#include "common/common_types.h"
#include "core/settings.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"

class RasterizerCacheObject {
public:
    explicit RasterizerCacheObject(const u8* host_ptr)
        : host_ptr{host_ptr}, cache_addr{ToCacheAddr(host_ptr)} {}

    virtual ~RasterizerCacheObject();

    CacheAddr GetCacheAddr() const {
        return cache_addr;
    }

    const u8* GetHostPtr() const {
        return host_ptr;
    }

    /// Gets the address of the shader in guest memory, required for cache management
    virtual VAddr GetCpuAddr() const = 0;

    /// Gets the size of the shader in guest memory, required for cache management
    virtual std::size_t GetSizeInBytes() const = 0;

    /// Wriets any cached resources back to memory
    virtual void Flush() = 0;

    /// Sets whether the cached object should be considered registered
    void SetIsRegistered(bool registered) {
        is_registered = registered;
    }

    /// Returns true if the cached object is registered
    bool IsRegistered() const {
        return is_registered;
    }

    /// Returns true if the cached object is dirty
    bool IsDirty() const {
        return is_dirty;
    }

    /// Returns ticks from when this cached object was last modified
    u64 GetLastModifiedTicks() const {
        return last_modified_ticks;
    }

    /// Marks an object as recently modified, used to specify whether it is clean or dirty
    template <class T>
    void MarkAsModified(bool dirty, T& cache) {
        is_dirty = dirty;
        last_modified_ticks = cache.GetModifiedTicks();
    }

private:
    bool is_registered{};      ///< Whether the object is currently registered with the cache
    bool is_dirty{};           ///< Whether the object is dirty (out of sync with guest memory)
    u64 last_modified_ticks{}; ///< When the object was last modified, used for in-order flushing
    CacheAddr cache_addr{};    ///< Cache address memory, unique from emulated virtual address space
    const u8* host_ptr{};      ///< Pointer to the memory backing this cached region
};

template <class T>
class RasterizerCache : NonCopyable {
    friend class RasterizerCacheObject;

public:
    explicit RasterizerCache(VideoCore::RasterizerInterface& rasterizer) : rasterizer{rasterizer} {}

    /// Write any cached resources overlapping the specified region back to memory
    void FlushRegion(CacheAddr addr, std::size_t size) {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        const auto& objects{GetSortedObjectsFromRegion(addr, size)};
        for (auto& object : objects) {
            FlushObject(object);
        }
    }

    /// Mark the specified region as being invalidated
    void InvalidateRegion(CacheAddr addr, u64 size) {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        const auto& objects{GetSortedObjectsFromRegion(addr, size)};
        for (auto& object : objects) {
            if (!object->IsRegistered()) {
                // Skip duplicates
                continue;
            }
            Unregister(object);
        }
    }

    /// Invalidates everything in the cache
    void InvalidateAll() {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        while (interval_cache.begin() != interval_cache.end()) {
            Unregister(*interval_cache.begin()->second.begin());
        }
    }

protected:
    /// Tries to get an object from the cache with the specified cache address
    T TryGet(CacheAddr addr) const {
        const auto iter = map_cache.find(addr);
        if (iter != map_cache.end())
            return iter->second;
        return nullptr;
    }

    T TryGet(const void* addr) const {
        const auto iter = map_cache.find(ToCacheAddr(addr));
        if (iter != map_cache.end())
            return iter->second;
        return nullptr;
    }

    /// Register an object into the cache
    void Register(const T& object) {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        object->SetIsRegistered(true);
        interval_cache.add({GetInterval(object), ObjectSet{object}});
        map_cache.insert({object->GetCacheAddr(), object});
        rasterizer.UpdatePagesCachedCount(object->GetCpuAddr(), object->GetSizeInBytes(), 1);
    }

    /// Unregisters an object from the cache
    void Unregister(const T& object) {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        object->SetIsRegistered(false);
        rasterizer.UpdatePagesCachedCount(object->GetCpuAddr(), object->GetSizeInBytes(), -1);
        interval_cache.subtract({GetInterval(object), ObjectSet{object}});
        map_cache.erase(object->GetCacheAddr());
    }

    /// Returns a ticks counter used for tracking when cached objects were last modified
    u64 GetModifiedTicks() {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        return ++modified_ticks;
    }

    /// Flushes the specified object, updating appropriate cache state as needed
    void FlushObject(const T& object) {
        std::lock_guard<std::recursive_mutex> lock{mutex};

        if (!object->IsDirty()) {
            return;
        }
        object->Flush();
        object->MarkAsModified(false, *this);
    }

private:
    /// Returns a list of cached objects from the specified memory region, ordered by access time
    std::vector<T> GetSortedObjectsFromRegion(CacheAddr addr, u64 size) {
        if (size == 0) {
            return {};
        }

        std::vector<T> objects;
        const ObjectInterval interval{addr, addr + size};
        for (auto& pair : boost::make_iterator_range(interval_cache.equal_range(interval))) {
            for (auto& cached_object : pair.second) {
                if (!cached_object) {
                    continue;
                }
                objects.push_back(cached_object);
            }
        }

        std::sort(objects.begin(), objects.end(), [](const T& a, const T& b) -> bool {
            return a->GetLastModifiedTicks() < b->GetLastModifiedTicks();
        });

        return objects;
    }

    using ObjectSet = std::set<T>;
    using ObjectCache = std::unordered_map<CacheAddr, T>;
    using IntervalCache = boost::icl::interval_map<CacheAddr, ObjectSet>;
    using ObjectInterval = typename IntervalCache::interval_type;

    static auto GetInterval(const T& object) {
        return ObjectInterval::right_open(object->GetCacheAddr(),
                                          object->GetCacheAddr() + object->GetSizeInBytes());
    }

    ObjectCache map_cache;
    IntervalCache interval_cache; ///< Cache of objects
    u64 modified_ticks{};         ///< Counter of cache state ticks, used for in-order flushing
    VideoCore::RasterizerInterface& rasterizer;
    std::recursive_mutex mutex;
};
