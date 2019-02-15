// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <optional>
#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_resource_manager.h"

namespace Vulkan {

// TODO(Rodrigo): Fine tune these numbers.
constexpr std::size_t COMMAND_BUFFER_POOL_SIZE = 0x1000;
constexpr std::size_t FENCES_GROW_STEP = 0x40;

class CommandBufferPool final : public VKFencedPool {
public:
    CommandBufferPool(const VKDevice& device)
        : VKFencedPool(COMMAND_BUFFER_POOL_SIZE), device{device} {}

    void Allocate(std::size_t begin, std::size_t end) {
        const auto dev = device.GetLogical();
        const auto& dld = device.GetDispatchLoader();
        const u32 graphics_family = device.GetGraphicsFamily();

        auto pool = std::make_unique<Pool>();

        // Command buffers are going to be commited, recorded, executed every single usage cycle.
        // They are also going to be reseted when commited.
        const auto pool_flags = vk::CommandPoolCreateFlagBits::eTransient |
                                vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        const vk::CommandPoolCreateInfo cmdbuf_pool_ci(pool_flags, graphics_family);
        pool->handle = dev.createCommandPoolUnique(cmdbuf_pool_ci, nullptr, dld);

        const vk::CommandBufferAllocateInfo cmdbuf_ai(*pool->handle,
                                                      vk::CommandBufferLevel::ePrimary,
                                                      static_cast<u32>(COMMAND_BUFFER_POOL_SIZE));
        pool->cmdbufs =
            dev.allocateCommandBuffersUnique<std::allocator<UniqueCommandBuffer>>(cmdbuf_ai, dld);

        pools.push_back(std::move(pool));
    }

    vk::CommandBuffer Commit(VKFence& fence) {
        const std::size_t index = CommitResource(fence);
        const auto pool_index = index / COMMAND_BUFFER_POOL_SIZE;
        const auto sub_index = index % COMMAND_BUFFER_POOL_SIZE;
        return *pools[pool_index]->cmdbufs[sub_index];
    }

private:
    struct Pool {
        UniqueCommandPool handle;
        std::vector<UniqueCommandBuffer> cmdbufs;
    };

    const VKDevice& device;

    std::vector<std::unique_ptr<Pool>> pools;
};

VKResource::VKResource() = default;

VKResource::~VKResource() = default;

VKFence::VKFence(const VKDevice& device, UniqueFence handle)
    : device{device}, handle{std::move(handle)} {}

VKFence::~VKFence() = default;

void VKFence::Wait() {
    const auto dev = device.GetLogical();
    const auto& dld = device.GetDispatchLoader();
    dev.waitForFences({*handle}, true, std::numeric_limits<u64>::max(), dld);
}

void VKFence::Release() {
    is_owned = false;
}

void VKFence::Commit() {
    is_owned = true;
    is_used = true;
}

bool VKFence::Tick(bool gpu_wait, bool owner_wait) {
    if (!is_used) {
        // If a fence is not used it's always free.
        return true;
    }
    if (is_owned && !owner_wait) {
        // The fence is still being owned (Release has not been called) and ownership wait has
        // not been asked.
        return false;
    }

    const auto dev = device.GetLogical();
    const auto& dld = device.GetDispatchLoader();
    if (gpu_wait) {
        // Wait for the fence if it has been requested.
        dev.waitForFences({*handle}, true, std::numeric_limits<u64>::max(), dld);
    } else {
        if (dev.getFenceStatus(*handle, dld) != vk::Result::eSuccess) {
            // Vulkan fence is not ready, not much it can do here
            return false;
        }
    }

    // Broadcast resources their free state.
    for (auto* resource : protected_resources) {
        resource->OnFenceRemoval(this);
    }
    protected_resources.clear();

    // Prepare fence for reusage.
    dev.resetFences({*handle}, dld);
    is_used = false;
    return true;
}

void VKFence::Protect(VKResource* resource) {
    protected_resources.push_back(resource);
}

void VKFence::Unprotect(const VKResource* resource) {
    const auto it = std::find(protected_resources.begin(), protected_resources.end(), resource);
    if (it != protected_resources.end()) {
        protected_resources.erase(it);
    }
}

VKFenceWatch::VKFenceWatch() = default;

VKFenceWatch::~VKFenceWatch() {
    if (fence) {
        fence->Unprotect(this);
    }
}

void VKFenceWatch::Wait() {
    if (!fence) {
        return;
    }
    fence->Wait();
    fence->Unprotect(this);
    fence = nullptr;
}

void VKFenceWatch::Watch(VKFence& new_fence) {
    Wait();
    fence = &new_fence;
    fence->Protect(this);
}

bool VKFenceWatch::TryWatch(VKFence& new_fence) {
    if (fence) {
        return false;
    }
    fence = &new_fence;
    fence->Protect(this);
    return true;
}

void VKFenceWatch::OnFenceRemoval(VKFence* signaling_fence) {
    ASSERT_MSG(signaling_fence == fence, "Removing the wrong fence");
    fence = nullptr;
}

VKFencedPool::VKFencedPool(std::size_t grow_step) : grow_step{grow_step} {}

VKFencedPool::~VKFencedPool() = default;

std::size_t VKFencedPool::CommitResource(VKFence& fence) {
    const auto Search = [&](std::size_t begin, std::size_t end) -> std::optional<std::size_t> {
        for (std::size_t iterator = begin; iterator < end; ++iterator) {
            if (watches[iterator]->TryWatch(fence)) {
                // The resource is now being watched, a free resource was successfully found.
                return iterator;
            }
        }
        return {};
    };
    // Try to find a free resource from the hinted position to the end.
    auto found = Search(free_iterator, watches.size());
    if (!found) {
        // Search from beginning to the hinted position.
        found = Search(0, free_iterator);
        if (!found) {
            // Both searches failed, the pool is full; handle it.
            const std::size_t free_resource = ManageOverflow();

            // Watch will wait for the resource to be free.
            watches[free_resource]->Watch(fence);
            found = free_resource;
        }
    }
    // Free iterator is hinted to the resource after the one that's been commited.
    free_iterator = (*found + 1) % watches.size();
    return *found;
}

std::size_t VKFencedPool::ManageOverflow() {
    const std::size_t old_capacity = watches.size();
    Grow();

    // The last entry is guaranted to be free, since it's the first element of the freshly
    // allocated resources.
    return old_capacity;
}

void VKFencedPool::Grow() {
    const std::size_t old_capacity = watches.size();
    watches.resize(old_capacity + grow_step);
    std::generate(watches.begin() + old_capacity, watches.end(),
                  []() { return std::make_unique<VKFenceWatch>(); });
    Allocate(old_capacity, old_capacity + grow_step);
}

VKResourceManager::VKResourceManager(const VKDevice& device) : device{device} {
    GrowFences(FENCES_GROW_STEP);
    command_buffer_pool = std::make_unique<CommandBufferPool>(device);
}

VKResourceManager::~VKResourceManager() = default;

VKFence& VKResourceManager::CommitFence() {
    const auto StepFences = [&](bool gpu_wait, bool owner_wait) -> VKFence* {
        const auto Tick = [=](auto& fence) { return fence->Tick(gpu_wait, owner_wait); };
        const auto hinted = fences.begin() + fences_iterator;

        auto it = std::find_if(hinted, fences.end(), Tick);
        if (it == fences.end()) {
            it = std::find_if(fences.begin(), hinted, Tick);
            if (it == hinted) {
                return nullptr;
            }
        }
        fences_iterator = std::distance(fences.begin(), it) + 1;
        if (fences_iterator >= fences.size())
            fences_iterator = 0;

        auto& fence = *it;
        fence->Commit();
        return fence.get();
    };

    VKFence* found_fence = StepFences(false, false);
    if (!found_fence) {
        // Try again, this time waiting.
        found_fence = StepFences(true, false);

        if (!found_fence) {
            // Allocate new fences and try again.
            LOG_INFO(Render_Vulkan, "Allocating new fences {} -> {}", fences.size(),
                     fences.size() + FENCES_GROW_STEP);

            GrowFences(FENCES_GROW_STEP);
            found_fence = StepFences(true, false);
            ASSERT(found_fence != nullptr);
        }
    }
    return *found_fence;
}

vk::CommandBuffer VKResourceManager::CommitCommandBuffer(VKFence& fence) {
    return command_buffer_pool->Commit(fence);
}

void VKResourceManager::GrowFences(std::size_t new_fences_count) {
    const auto dev = device.GetLogical();
    const auto& dld = device.GetDispatchLoader();
    const vk::FenceCreateInfo fence_ci;

    const std::size_t previous_size = fences.size();
    fences.resize(previous_size + new_fences_count);

    std::generate(fences.begin() + previous_size, fences.end(), [&]() {
        return std::make_unique<VKFence>(device, dev.createFenceUnique(fence_ci, nullptr, dld));
    });
}

} // namespace Vulkan
