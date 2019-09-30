// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include "common/common_types.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/nvflinger/buffer_queue.h"
#include "video_core/dma_pusher.h"

using CacheAddr = std::uintptr_t;
inline CacheAddr ToCacheAddr(const void* host_ptr) {
    return reinterpret_cast<CacheAddr>(host_ptr);
}

inline u8* FromCacheAddr(CacheAddr cache_addr) {
    return reinterpret_cast<u8*>(cache_addr);
}

namespace Core {
class System;
}

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace Tegra {

enum class RenderTargetFormat : u32 {
    NONE = 0x0,
    RGBA32_FLOAT = 0xC0,
    RGBA32_UINT = 0xC2,
    RGBA16_UNORM = 0xC6,
    RGBA16_UINT = 0xC9,
    RGBA16_FLOAT = 0xCA,
    RG32_FLOAT = 0xCB,
    RG32_UINT = 0xCD,
    RGBX16_FLOAT = 0xCE,
    BGRA8_UNORM = 0xCF,
    BGRA8_SRGB = 0xD0,
    RGB10_A2_UNORM = 0xD1,
    RGBA8_UNORM = 0xD5,
    RGBA8_SRGB = 0xD6,
    RGBA8_SNORM = 0xD7,
    RGBA8_UINT = 0xD9,
    RG16_UNORM = 0xDA,
    RG16_SNORM = 0xDB,
    RG16_SINT = 0xDC,
    RG16_UINT = 0xDD,
    RG16_FLOAT = 0xDE,
    R11G11B10_FLOAT = 0xE0,
    R32_UINT = 0xE4,
    R32_FLOAT = 0xE5,
    B5G6R5_UNORM = 0xE8,
    BGR5A1_UNORM = 0xE9,
    RG8_UNORM = 0xEA,
    RG8_SNORM = 0xEB,
    R16_UNORM = 0xEE,
    R16_SNORM = 0xEF,
    R16_SINT = 0xF0,
    R16_UINT = 0xF1,
    R16_FLOAT = 0xF2,
    R8_UNORM = 0xF3,
    R8_UINT = 0xF6,
};

enum class DepthFormat : u32 {
    Z32_FLOAT = 0xA,
    Z16_UNORM = 0x13,
    S8_Z24_UNORM = 0x14,
    Z24_X8_UNORM = 0x15,
    Z24_S8_UNORM = 0x16,
    Z24_C8_UNORM = 0x18,
    Z32_S8_X24_FLOAT = 0x19,
};

/// Returns the number of bytes per pixel of each rendertarget format.
u32 RenderTargetBytesPerPixel(RenderTargetFormat format);

/// Returns the number of bytes per pixel of each depth format.
u32 DepthFormatBytesPerPixel(DepthFormat format);

struct CommandListHeader;
class DebugContext;

/**
 * Struct describing framebuffer configuration
 */
struct FramebufferConfig {
    enum class PixelFormat : u32 {
        ABGR8 = 1,
        RGB565 = 4,
        BGRA8 = 5,
    };

    VAddr address;
    u32 offset;
    u32 width;
    u32 height;
    u32 stride;
    PixelFormat pixel_format;

    using TransformFlags = Service::NVFlinger::BufferQueue::BufferTransformFlags;
    TransformFlags transform_flags;
    Common::Rectangle<int> crop_rect;
};

namespace Engines {
class Fermi2D;
class Maxwell3D;
class MaxwellDMA;
class KeplerCompute;
class KeplerMemory;
} // namespace Engines

enum class EngineID {
    FERMI_TWOD_A = 0x902D, // 2D Engine
    MAXWELL_B = 0xB197,    // 3D Engine
    KEPLER_COMPUTE_B = 0xB1C0,
    KEPLER_INLINE_TO_MEMORY_B = 0xA140,
    MAXWELL_DMA_COPY_A = 0xB0B5,
};

class MemoryManager;

class GPU {
public:
    explicit GPU(Core::System& system, VideoCore::RendererBase& renderer, bool is_async);

    virtual ~GPU();

    struct MethodCall {
        u32 method{};
        u32 argument{};
        u32 subchannel{};
        u32 method_count{};

        bool IsLastCall() const {
            return method_count <= 1;
        }

        MethodCall(u32 method, u32 argument, u32 subchannel = 0, u32 method_count = 0)
            : method(method), argument(argument), subchannel(subchannel),
              method_count(method_count) {}
    };

    /// Calls a GPU method.
    void CallMethod(const MethodCall& method_call);

    void FlushCommands();

    /// Returns a reference to the Maxwell3D GPU engine.
    Engines::Maxwell3D& Maxwell3D();

    /// Returns a const reference to the Maxwell3D GPU engine.
    const Engines::Maxwell3D& Maxwell3D() const;

    /// Returns a reference to the KeplerCompute GPU engine.
    Engines::KeplerCompute& KeplerCompute();

    /// Returns a reference to the KeplerCompute GPU engine.
    const Engines::KeplerCompute& KeplerCompute() const;

    /// Returns a reference to the GPU memory manager.
    Tegra::MemoryManager& MemoryManager();

    /// Returns a const reference to the GPU memory manager.
    const Tegra::MemoryManager& MemoryManager() const;

    /// Returns a reference to the GPU DMA pusher.
    Tegra::DmaPusher& DmaPusher();

    // Waits for the GPU to finish working
    virtual void WaitIdle() const = 0;

    /// Allows the CPU/NvFlinger to wait on the GPU before presenting a frame.
    void WaitFence(u32 syncpoint_id, u32 value) const;

    void IncrementSyncPoint(u32 syncpoint_id);

    u32 GetSyncpointValue(u32 syncpoint_id) const;

    void RegisterSyncptInterrupt(u32 syncpoint_id, u32 value);

    bool CancelSyncptInterrupt(u32 syncpoint_id, u32 value);

    std::unique_lock<std::mutex> LockSync() {
        return std::unique_lock{sync_mutex};
    }

    bool IsAsync() const {
        return is_async;
    }

    /// Returns a const reference to the GPU DMA pusher.
    const Tegra::DmaPusher& DmaPusher() const;

    struct Regs {
        static constexpr size_t NUM_REGS = 0x100;

        union {
            struct {
                INSERT_PADDING_WORDS(0x4);
                struct {
                    u32 address_high;
                    u32 address_low;

                    GPUVAddr SemaphoreAddress() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } semaphore_address;

                u32 semaphore_sequence;
                u32 semaphore_trigger;
                INSERT_PADDING_WORDS(0xC);

                // The puser and the puller share the reference counter, the pusher only has read
                // access
                u32 reference_count;
                INSERT_PADDING_WORDS(0x5);

                u32 semaphore_acquire;
                u32 semaphore_release;
                u32 fence_value;
                union {
                    BitField<4, 4, u32> operation;
                    BitField<8, 8, u32> id;
                } fence_action;
                INSERT_PADDING_WORDS(0xE2);

                // Puller state
                u32 acquire_mode;
                u32 acquire_source;
                u32 acquire_active;
                u32 acquire_timeout;
                u32 acquire_value;
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    /// Performs any additional setup necessary in order to begin GPU emulation.
    /// This can be used to launch any necessary threads and register any necessary
    /// core timing events.
    virtual void Start() = 0;

    /// Push GPU command entries to be processed
    virtual void PushGPUEntries(Tegra::CommandList&& entries) = 0;

    /// Swap buffers (render frame)
    virtual void SwapBuffers(const Tegra::FramebufferConfig* framebuffer) = 0;

    /// Notify rasterizer that any caches of the specified region should be flushed to Switch memory
    virtual void FlushRegion(CacheAddr addr, u64 size) = 0;

    /// Notify rasterizer that any caches of the specified region should be invalidated
    virtual void InvalidateRegion(CacheAddr addr, u64 size) = 0;

    /// Notify rasterizer that any caches of the specified region should be flushed and invalidated
    virtual void FlushAndInvalidateRegion(CacheAddr addr, u64 size) = 0;

protected:
    virtual void TriggerCpuInterrupt(u32 syncpoint_id, u32 value) const = 0;

private:
    void ProcessBindMethod(const MethodCall& method_call);
    void ProcessSemaphoreTriggerMethod();
    void ProcessSemaphoreRelease();
    void ProcessSemaphoreAcquire();

    /// Calls a GPU puller method.
    void CallPullerMethod(const MethodCall& method_call);

    /// Calls a GPU engine method.
    void CallEngineMethod(const MethodCall& method_call);

    /// Determines where the method should be executed.
    bool ExecuteMethodOnEngine(const MethodCall& method_call);

protected:
    std::unique_ptr<Tegra::DmaPusher> dma_pusher;
    Core::System& system;
    VideoCore::RendererBase& renderer;

private:
    std::unique_ptr<Tegra::MemoryManager> memory_manager;

    /// Mapping of command subchannels to their bound engine ids
    std::array<EngineID, 8> bound_engines = {};
    /// 3D engine
    std::unique_ptr<Engines::Maxwell3D> maxwell_3d;
    /// 2D engine
    std::unique_ptr<Engines::Fermi2D> fermi_2d;
    /// Compute engine
    std::unique_ptr<Engines::KeplerCompute> kepler_compute;
    /// DMA engine
    std::unique_ptr<Engines::MaxwellDMA> maxwell_dma;
    /// Inline memory engine
    std::unique_ptr<Engines::KeplerMemory> kepler_memory;

    std::array<std::atomic<u32>, Service::Nvidia::MaxSyncPoints> syncpoints{};

    std::array<std::list<u32>, Service::Nvidia::MaxSyncPoints> syncpt_interrupts;

    std::mutex sync_mutex;

    const bool is_async;
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(GPU::Regs, field_name) == position * 4,                                 \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(semaphore_address, 0x4);
ASSERT_REG_POSITION(semaphore_sequence, 0x6);
ASSERT_REG_POSITION(semaphore_trigger, 0x7);
ASSERT_REG_POSITION(reference_count, 0x14);
ASSERT_REG_POSITION(semaphore_acquire, 0x1A);
ASSERT_REG_POSITION(semaphore_release, 0x1B);
ASSERT_REG_POSITION(fence_value, 0x1C);
ASSERT_REG_POSITION(fence_action, 0x1D);

ASSERT_REG_POSITION(acquire_mode, 0x100);
ASSERT_REG_POSITION(acquire_source, 0x101);
ASSERT_REG_POSITION(acquire_active, 0x102);
ASSERT_REG_POSITION(acquire_timeout, 0x103);
ASSERT_REG_POSITION(acquire_value, 0x104);

#undef ASSERT_REG_POSITION

} // namespace Tegra
