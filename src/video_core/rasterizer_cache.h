// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <set>

#include <boost/icl/interval_map.hpp>

#include "common/common_types.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

template <class T>
class RasterizerCache : NonCopyable {
public:
    /// Mark the specified page as being invalidated
    void InvalidateRegion(VAddr addr, u64 size) {
        if (size == 0)
            return;

        const ObjectInterval interval{addr, addr + size};
        for (auto& pair : boost::make_iterator_range(object_cache.equal_range(interval))) {
            for (auto& cached_object : pair.second) {
                if (!cached_object)
                    continue;

                remove_objects.emplace(cached_object);
            }
        }

        for (auto& remove_object : remove_objects) {
            Unregister(remove_object);
        }

        remove_objects.clear();
    }

protected:
    /// Tries to get an object from the cache with the specified address
    T TryGet(VAddr addr) const {
        const ObjectInterval interval{addr};
        for (auto& pair : boost::make_iterator_range(object_cache.equal_range(interval))) {
            for (auto& cached_object : pair.second) {
                if (cached_object->GetAddr() == addr) {
                    return cached_object;
                }
            }
        }
        return nullptr;
    }

    /// Register an object into the cache
    void Register(const T& object) {
        object_cache.add({GetInterval(object), ObjectSet{object}});
        auto& rasterizer = Core::System::GetInstance().Renderer().Rasterizer();
        rasterizer.UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), 1);
    }

    /// Unregisters an object from the cache
    void Unregister(const T& object) {
        auto& rasterizer = Core::System::GetInstance().Renderer().Rasterizer();
        rasterizer.UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), -1);
        object_cache.subtract({GetInterval(object), ObjectSet{object}});
    }

private:
    using ObjectSet = std::set<T>;
    using ObjectCache = boost::icl::interval_map<VAddr, ObjectSet>;
    using ObjectInterval = typename ObjectCache::interval_type;

    static auto GetInterval(const T& object) {
        return ObjectInterval::right_open(object->GetAddr(),
                                          object->GetAddr() + object->GetSizeInBytes());
    }

    ObjectCache object_cache;
    ObjectSet remove_objects;
};
