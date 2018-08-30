// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_map>
#include <vector>

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
    void InvalidatePage(u64 page) {
        const auto& search_by_page{cached_pages.find(page)};
        if (search_by_page == cached_pages.end()) {
            return;
        }

        for (const auto& object : search_by_page->second) {
            Unregister(object.second, false);
        }

        cached_pages.erase(search_by_page);
    }

protected:
    /// Tries to get an object from the cache with the specified address
    T TryGet(VAddr addr) const {
        const u64 page{addr >> Memory::PAGE_BITS};
        const auto& search_by_page{cached_pages.find(page)};
        if (search_by_page == cached_pages.end()) {
            return nullptr;
        }

        const auto& search_by_addr{search_by_page->second.find(addr)};
        if (search_by_addr == search_by_page->second.end()) {
            return nullptr;
        }

        return search_by_addr->second;
    }

    /// Register an object into the cache
    void Register(const T& object) {
        const u64 page{object->GetAddr() >> Memory::PAGE_BITS};
        const auto& search_by_page{cached_pages.find(page)};
        if (search_by_page != cached_pages.end()) {
            search_by_page->second[object->GetAddr()] = object;
        } else {
            CacheMap new_cache_map;
            new_cache_map[object->GetAddr()] = object;
            cached_pages[page] = std::move(new_cache_map);
        }

        auto& rasterizer = Core::System::GetInstance().Renderer().Rasterizer();
        rasterizer.UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), 1);
    }

    /// Unregisters an object from the cache
    void Unregister(const T& object, bool remove_from_cache = true) {
        const u64 page{object->GetAddr() >> Memory::PAGE_BITS};
        const auto& search_by_page{cached_pages.find(page)};
        if (search_by_page == cached_pages.end()) {
            // Unregistered already
            return;
        }

        const auto& search_by_addr{search_by_page->second.find(object->GetAddr())};
        if (search_by_addr == search_by_page->second.end()) {
            // Unregistered already
            return;
        }

        auto& rasterizer = Core::System::GetInstance().Renderer().Rasterizer();
        rasterizer.UpdatePagesCachedCount(object->GetAddr(), object->GetSizeInBytes(), -1);

        // Remove from the cache if specified
        if (remove_from_cache) {
            if (search_by_page->second.size() == 1) {
                // Only one item at the page, remove the cached page
                cached_pages.erase(search_by_page);
            } else {
                // Remove just the item at the cached page
                search_by_page->second.erase(search_by_addr);
            }
        }
    }

private:
    using CacheMap = std::unordered_map<VAddr, T>;

    /// Two-level cache of objects, where first level is keyed on starting CPU address page, and
    /// second level is keyed on exact CPU address
    std::unordered_map<u64, CacheMap> cached_pages;
};
