// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_map>
#include <boost/icl/interval_map.hpp>
#include <boost/range/iterator_range.hpp>

#include "common/common_types.h"
#include "core/memory.h"
#include "video_core/memory_manager.h"

template <class T>
class RasterizerCache : NonCopyable {
public:
    /// Mark the specified region as being invalidated
    void InvalidateRegion(Tegra::GPUVAddr region_addr, size_t region_size) {
        for (auto iter = cached_objects.cbegin(); iter != cached_objects.cend();) {
            const auto& object{iter->second};

            ++iter;

            if (object->GetAddr() <= (region_addr + region_size) &&
                region_addr <= (object->GetAddr() + object->GetSizeInBytes())) {
                // Regions overlap, so invalidate
                Unregister(object);
            }
        }
    }

protected:
    /// Tries to get an object from the cache with the specified address
    T TryGet(Tegra::GPUVAddr addr) const {
        const auto& search{cached_objects.find(addr)};
        if (search != cached_objects.end()) {
            return search->second;
        }

        return nullptr;
    }

    /// Gets a reference to the cache
    const std::unordered_map<Tegra::GPUVAddr, T>& GetCache() const {
        return cached_objects;
    }

    /// Register an object into the cache
    void Register(const T& object) {
        const auto& search{cached_objects.find(object->GetAddr())};
        if (search != cached_objects.end()) {
            // Registered already
            return;
        }

        cached_objects[object->GetAddr()] = object;
        UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), 1);
    }

    /// Unregisters an object from the cache
    void Unregister(const T& object) {
        const auto& search{cached_objects.find(object->GetAddr())};
        if (search == cached_objects.end()) {
            // Unregistered already
            return;
        }

        UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), -1);
        cached_objects.erase(search);
    }

private:
    using PageMap = boost::icl::interval_map<u64, int>;

    template <typename Map, typename Interval>
    constexpr auto RangeFromInterval(Map& map, const Interval& interval) {
        return boost::make_iterator_range(map.equal_range(interval));
    }

    /// Increase/decrease the number of object in pages touching the specified region
    void UpdatePagesCachedCount(Tegra::GPUVAddr addr, u64 size, int delta) {
        const u64 page_start{addr >> Tegra::MemoryManager::PAGE_BITS};
        const u64 page_end{(addr + size) >> Tegra::MemoryManager::PAGE_BITS};

        // Interval maps will erase segments if count reaches 0, so if delta is negative we have to
        // subtract after iterating
        const auto pages_interval = PageMap::interval_type::right_open(page_start, page_end);
        if (delta > 0)
            cached_pages.add({pages_interval, delta});

        for (const auto& pair : RangeFromInterval(cached_pages, pages_interval)) {
            const auto interval = pair.first & pages_interval;
            const int count = pair.second;

            const Tegra::GPUVAddr interval_start_addr = boost::icl::first(interval)
                                                        << Tegra::MemoryManager::PAGE_BITS;
            const Tegra::GPUVAddr interval_end_addr = boost::icl::last_next(interval)
                                                      << Tegra::MemoryManager::PAGE_BITS;
            const u64 interval_size = interval_end_addr - interval_start_addr;

            if (delta > 0 && count == delta)
                Memory::RasterizerMarkRegionCached(interval_start_addr, interval_size, true);
            else if (delta < 0 && count == -delta)
                Memory::RasterizerMarkRegionCached(interval_start_addr, interval_size, false);
            else
                ASSERT(count >= 0);
        }

        if (delta < 0)
            cached_pages.add({pages_interval, delta});
    }

    std::unordered_map<Tegra::GPUVAddr, T> cached_objects;
    PageMap cached_pages;
};
