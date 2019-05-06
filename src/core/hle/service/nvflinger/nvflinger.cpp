// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <optional>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvflinger/buffer_queue.h"
#include "core/hle/service/nvflinger/nvflinger.h"
#include "core/hle/service/vi/display/vi_display.h"
#include "core/hle/service/vi/layer/vi_layer.h"
#include "core/perf_stats.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace Service::NVFlinger {

constexpr s64 frame_ticks = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / 60);
constexpr s64 frame_ticks_30fps = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / fps_mode);

NVFlinger::NVFlinger(Core::Timing::CoreTiming& core_timing) : core_timing{core_timing} {
    displays.emplace_back(0, "Default");
    displays.emplace_back(1, "External");
    displays.emplace_back(2, "Edid");
    displays.emplace_back(3, "Internal");
    displays.emplace_back(4, "Null");

    // Schedule the screen composition events
    const auto ticks = Settings::values.force_30fps_mode ? frame_ticks_30fps : frame_ticks;

    composition_event = core_timing.RegisterEvent(
        "ScreenComposition", [this, ticks](u64 userdata, s64 cycles_late) {
            Compose();
            this->core_timing.ScheduleEvent(ticks - cycles_late, composition_event);
        });

    core_timing.ScheduleEvent(ticks, composition_event);
}

NVFlinger::~NVFlinger() {
    core_timing.UnscheduleEvent(composition_event, 0);
}

void NVFlinger::SetNVDrvInstance(std::shared_ptr<Nvidia::Module> instance) {
    nvdrv = std::move(instance);
}

std::optional<u64> NVFlinger::OpenDisplay(std::string_view name) {
    LOG_DEBUG(Service, "Opening \"{}\" display", name);

    // TODO(Subv): Currently we only support the Default display.
    ASSERT(name == "Default");

    const auto itr =
        std::find_if(displays.begin(), displays.end(),
                     [&](const VI::Display& display) { return display.GetName() == name; });

    if (itr == displays.end()) {
        return {};
    }

    return itr->GetID();
}

std::optional<u64> NVFlinger::CreateLayer(u64 display_id) {
    auto* const display = FindDisplay(display_id);

    if (display == nullptr) {
        return {};
    }

    const u64 layer_id = next_layer_id++;
    const u32 buffer_queue_id = next_buffer_queue_id++;
    buffer_queues.emplace_back(buffer_queue_id, layer_id);
    display->CreateLayer(layer_id, buffer_queues.back());
    return layer_id;
}

std::optional<u32> NVFlinger::FindBufferQueueId(u64 display_id, u64 layer_id) const {
    const auto* const layer = FindLayer(display_id, layer_id);

    if (layer == nullptr) {
        return {};
    }

    return layer->GetBufferQueue().GetId();
}

Kernel::SharedPtr<Kernel::ReadableEvent> NVFlinger::FindVsyncEvent(u64 display_id) const {
    auto* const display = FindDisplay(display_id);

    if (display == nullptr) {
        return nullptr;
    }

    return display->GetVSyncEvent();
}

BufferQueue& NVFlinger::FindBufferQueue(u32 id) {
    const auto itr = std::find_if(buffer_queues.begin(), buffer_queues.end(),
                                  [id](const auto& queue) { return queue.GetId() == id; });

    ASSERT(itr != buffer_queues.end());
    return *itr;
}

const BufferQueue& NVFlinger::FindBufferQueue(u32 id) const {
    const auto itr = std::find_if(buffer_queues.begin(), buffer_queues.end(),
                                  [id](const auto& queue) { return queue.GetId() == id; });

    ASSERT(itr != buffer_queues.end());
    return *itr;
}

VI::Display* NVFlinger::FindDisplay(u64 display_id) {
    const auto itr =
        std::find_if(displays.begin(), displays.end(),
                     [&](const VI::Display& display) { return display.GetID() == display_id; });

    if (itr == displays.end()) {
        return nullptr;
    }

    return &*itr;
}

const VI::Display* NVFlinger::FindDisplay(u64 display_id) const {
    const auto itr =
        std::find_if(displays.begin(), displays.end(),
                     [&](const VI::Display& display) { return display.GetID() == display_id; });

    if (itr == displays.end()) {
        return nullptr;
    }

    return &*itr;
}

VI::Layer* NVFlinger::FindLayer(u64 display_id, u64 layer_id) {
    auto* const display = FindDisplay(display_id);

    if (display == nullptr) {
        return nullptr;
    }

    return display->FindLayer(layer_id);
}

const VI::Layer* NVFlinger::FindLayer(u64 display_id, u64 layer_id) const {
    const auto* const display = FindDisplay(display_id);

    if (display == nullptr) {
        return nullptr;
    }

    return display->FindLayer(layer_id);
}

void NVFlinger::Compose() {
    for (auto& display : displays) {
        // Trigger vsync for this display at the end of drawing
        SCOPE_EXIT({ display.SignalVSyncEvent(); });

        // Don't do anything for displays without layers.
        if (!display.HasLayers())
            continue;

        // TODO(Subv): Support more than 1 layer.
        VI::Layer& layer = display.GetLayer(0);
        auto& buffer_queue = layer.GetBufferQueue();

        // Search for a queued buffer and acquire it
        auto buffer = buffer_queue.AcquireBuffer();

        MicroProfileFlip();

        if (!buffer) {
            auto& system_instance = Core::System::GetInstance();

            // There was no queued buffer to draw, render previous frame
            system_instance.GetPerfStats().EndGameFrame();
            system_instance.GPU().SwapBuffers({});
            continue;
        }

        const auto& igbp_buffer = buffer->get().igbp_buffer;

        // Now send the buffer to the GPU for drawing.
        // TODO(Subv): Support more than just disp0. The display device selection is probably based
        // on which display we're drawing (Default, Internal, External, etc)
        auto nvdisp = nvdrv->GetDevice<Nvidia::Devices::nvdisp_disp0>("/dev/nvdisp_disp0");
        ASSERT(nvdisp);

        nvdisp->flip(igbp_buffer.gpu_buffer_id, igbp_buffer.offset, igbp_buffer.format,
                     igbp_buffer.width, igbp_buffer.height, igbp_buffer.stride,
                     buffer->get().transform, buffer->get().crop_rect);

        buffer_queue.ReleaseBuffer(buffer->get().slot);
    }
}

} // namespace Service::NVFlinger
