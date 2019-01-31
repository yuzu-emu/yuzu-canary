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
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvflinger/buffer_queue.h"
#include "core/hle/service/nvflinger/nvflinger.h"
#include "core/perf_stats.h"
#include "video_core/renderer_base.h"

namespace Service::NVFlinger {

constexpr std::size_t SCREEN_REFRESH_RATE = 60;
constexpr u64 frame_ticks = static_cast<u64>(CoreTiming::BASE_CLOCK_RATE / SCREEN_REFRESH_RATE);

NVFlinger::NVFlinger() {
    // Schedule the screen composition events
    composition_event =
        CoreTiming::RegisterEvent("ScreenComposition", [this](u64 userdata, int cycles_late) {
            Compose();
            CoreTiming::ScheduleEvent(frame_ticks - cycles_late, composition_event);
        });

    CoreTiming::ScheduleEvent(frame_ticks, composition_event);
}

NVFlinger::~NVFlinger() {
    CoreTiming::UnscheduleEvent(composition_event, 0);
}

void NVFlinger::SetNVDrvInstance(std::shared_ptr<Nvidia::Module> instance) {
    nvdrv = std::move(instance);
}

u64 NVFlinger::OpenDisplay(std::string_view name) {
    LOG_DEBUG(Service, "Opening \"{}\" display", name);

    // TODO(Subv): Currently we only support the Default display.
    ASSERT(name == "Default");

    const auto itr = std::find_if(displays.begin(), displays.end(),
                                  [&](const Display& display) { return display.name == name; });

    ASSERT(itr != displays.end());

    return itr->id;
}

u64 NVFlinger::CreateLayer(u64 display_id) {
    auto& display = FindDisplay(display_id);

    ASSERT_MSG(display.layers.empty(), "Only one layer is supported per display at the moment");

    const u64 layer_id = next_layer_id++;
    const u32 buffer_queue_id = next_buffer_queue_id++;
    auto buffer_queue = std::make_shared<BufferQueue>(buffer_queue_id, layer_id);
    display.layers.emplace_back(layer_id, buffer_queue);
    buffer_queues.emplace_back(std::move(buffer_queue));
    return layer_id;
}

u32 NVFlinger::FindBufferQueueId(u64 display_id, u64 layer_id) const {
    const auto& layer = FindLayer(display_id, layer_id);
    return layer.buffer_queue->GetId();
}

Kernel::SharedPtr<Kernel::ReadableEvent> NVFlinger::GetVsyncEvent(u64 display_id) {
    return FindDisplay(display_id).vsync_event.readable;
}

std::shared_ptr<BufferQueue> NVFlinger::FindBufferQueue(u32 id) const {
    const auto itr = std::find_if(buffer_queues.begin(), buffer_queues.end(),
                                  [&](const auto& queue) { return queue->GetId() == id; });

    ASSERT(itr != buffer_queues.end());
    return *itr;
}

Display& NVFlinger::FindDisplay(u64 display_id) {
    const auto itr = std::find_if(displays.begin(), displays.end(),
                                  [&](const Display& display) { return display.id == display_id; });

    ASSERT(itr != displays.end());
    return *itr;
}

const Display& NVFlinger::FindDisplay(u64 display_id) const {
    const auto itr = std::find_if(displays.begin(), displays.end(),
                                  [&](const Display& display) { return display.id == display_id; });

    ASSERT(itr != displays.end());
    return *itr;
}

Layer& NVFlinger::FindLayer(u64 display_id, u64 layer_id) {
    auto& display = FindDisplay(display_id);

    const auto itr = std::find_if(display.layers.begin(), display.layers.end(),
                                  [&](const Layer& layer) { return layer.id == layer_id; });

    ASSERT(itr != display.layers.end());
    return *itr;
}

const Layer& NVFlinger::FindLayer(u64 display_id, u64 layer_id) const {
    const auto& display = FindDisplay(display_id);

    const auto itr = std::find_if(display.layers.begin(), display.layers.end(),
                                  [&](const Layer& layer) { return layer.id == layer_id; });

    ASSERT(itr != display.layers.end());
    return *itr;
}

void NVFlinger::Compose() {
    for (auto& display : displays) {
        // Trigger vsync for this display at the end of drawing
        SCOPE_EXIT({ display.vsync_event.writable->Signal(); });

        // Don't do anything for displays without layers.
        if (display.layers.empty())
            continue;

        // TODO(Subv): Support more than 1 layer.
        ASSERT_MSG(display.layers.size() == 1, "Max 1 layer per display is supported");

        Layer& layer = display.layers[0];
        auto& buffer_queue = layer.buffer_queue;

        // Search for a queued buffer and acquire it
        auto buffer = buffer_queue->AcquireBuffer();

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

        buffer_queue->ReleaseBuffer(buffer->get().slot);
    }
}

Layer::Layer(u64 id, std::shared_ptr<BufferQueue> queue) : id(id), buffer_queue(std::move(queue)) {}
Layer::~Layer() = default;

Display::Display(u64 id, std::string name) : id(id), name(std::move(name)) {
    auto& kernel = Core::System::GetInstance().Kernel();
    vsync_event = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Sticky,
                                                         fmt::format("Display VSync Event {}", id));
}

Display::~Display() = default;

} // namespace Service::NVFlinger
