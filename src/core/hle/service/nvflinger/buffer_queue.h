// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <list>
#include <optional>
#include <vector>

#include "common/common_funcs.h"
#include "common/math_util.h"
#include "common/swap.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/nvdrv/nvdata.h"

namespace Service::NVFlinger {

struct IGBPBuffer {
    u32_le magic;
    u32_le width;
    u32_le height;
    u32_le stride;
    u32_le format;
    u32_le usage;
    INSERT_PADDING_WORDS(1);
    u32_le index;
    INSERT_PADDING_WORDS(3);
    u32_le gpu_buffer_id;
    INSERT_PADDING_WORDS(17);
    u32_le nvmap_handle;
    u32_le offset;
    INSERT_PADDING_WORDS(60);
};

static_assert(sizeof(IGBPBuffer) == 0x16C, "IGBPBuffer has wrong size");

class BufferQueue final {
public:
    enum class QueryType {
        NativeWindowWidth = 0,
        NativeWindowHeight = 1,
        NativeWindowFormat = 2,
    };

    BufferQueue(u32 id, u64 layer_id);
    ~BufferQueue();

    enum class BufferTransformFlags : u32 {
        /// No transform flags are set
        Unset = 0x00,
        /// Flip source image horizontally (around the vertical axis)
        FlipH = 0x01,
        /// Flip source image vertically (around the horizontal axis)
        FlipV = 0x02,
        /// Rotate source image 90 degrees clockwise
        Rotate90 = 0x04,
        /// Rotate source image 180 degrees
        Rotate180 = 0x03,
        /// Rotate source image 270 degrees clockwise
        Rotate270 = 0x07,
    };

    struct Buffer {
        enum class Status { Free = 0, Queued = 1, Dequeued = 2, Acquired = 3 };

        u32 slot;
        Status status = Status::Free;
        IGBPBuffer igbp_buffer;
        BufferTransformFlags transform;
        Common::Rectangle<int> crop_rect;
        u32 swap_interval;
        Service::Nvidia::MultiFence multi_fence;
    };

    void SetPreallocatedBuffer(u32 slot, const IGBPBuffer& igbp_buffer);
    std::optional<std::pair<u32, Service::Nvidia::MultiFence*>> DequeueBuffer(u32 width,
                                                                              u32 height);
    const IGBPBuffer& RequestBuffer(u32 slot) const;
    void QueueBuffer(u32 slot, BufferTransformFlags transform,
                     const Common::Rectangle<int>& crop_rect, u32 swap_interval,
                     Service::Nvidia::MultiFence& multi_fence);
    std::optional<std::reference_wrapper<const Buffer>> AcquireBuffer();
    void ReleaseBuffer(u32 slot);
    u32 Query(QueryType type);

    u32 GetId() const {
        return id;
    }

    Kernel::SharedPtr<Kernel::WritableEvent> GetWritableBufferWaitEvent() const;

    Kernel::SharedPtr<Kernel::ReadableEvent> GetBufferWaitEvent() const;

private:
    u32 id;
    u64 layer_id;

    std::vector<Buffer> queue;
    std::list<u32> queue_sequence;
    Kernel::EventPair buffer_wait_event;
};

} // namespace Service::NVFlinger
