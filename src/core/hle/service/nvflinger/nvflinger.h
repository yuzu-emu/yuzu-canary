// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"
#include "core/hle/kernel/object.h"

namespace Core::Timing {
class CoreTiming;
struct EventType;
} // namespace Core::Timing

namespace Kernel {
class ReadableEvent;
class WritableEvent;
} // namespace Kernel

namespace Service::Nvidia {
class Module;
} // namespace Service::Nvidia

namespace Service::VI {
class Display;
class Layer;
} // namespace Service::VI

namespace Service::NVFlinger {

class BufferQueue;

class NVFlinger final {
public:
    explicit NVFlinger(Core::Timing::CoreTiming& core_timing);
    ~NVFlinger();

    /// Sets the NVDrv module instance to use to send buffers to the GPU.
    void SetNVDrvInstance(std::shared_ptr<Nvidia::Module> instance);

    /// Opens the specified display and returns the ID.
    ///
    /// If an invalid display name is provided, then an empty optional is returned.
    std::optional<u64> OpenDisplay(std::string_view name);

    /// Creates a layer on the specified display and returns the layer ID.
    ///
    /// If an invalid display ID is specified, then an empty optional is returned.
    std::optional<u64> CreateLayer(u64 display_id);

    /// Finds the buffer queue ID of the specified layer in the specified display.
    ///
    /// If an invalid display ID or layer ID is provided, then an empty optional is returned.
    std::optional<u32> FindBufferQueueId(u64 display_id, u64 layer_id) const;

    /// Gets the vsync event for the specified display.
    ///
    /// If an invalid display ID is provided, then nullptr is returned.
    Kernel::SharedPtr<Kernel::ReadableEvent> FindVsyncEvent(u64 display_id) const;

    /// Obtains a buffer queue identified by the ID.
    BufferQueue& FindBufferQueue(u32 id);

    /// Obtains a buffer queue identified by the ID.
    const BufferQueue& FindBufferQueue(u32 id) const;

    /// Performs a composition request to the emulated nvidia GPU and triggers the vsync events when
    /// finished.
    void Compose();

    s64 GetNextTicks() const;

private:
    /// Finds the display identified by the specified ID.
    VI::Display* FindDisplay(u64 display_id);

    /// Finds the display identified by the specified ID.
    const VI::Display* FindDisplay(u64 display_id) const;

    /// Finds the layer identified by the specified ID in the desired display.
    VI::Layer* FindLayer(u64 display_id, u64 layer_id);

    /// Finds the layer identified by the specified ID in the desired display.
    const VI::Layer* FindLayer(u64 display_id, u64 layer_id) const;

    std::shared_ptr<Nvidia::Module> nvdrv;

    std::vector<VI::Display> displays;
    std::vector<BufferQueue> buffer_queues;

    /// Id to use for the next layer that is created, this counter is shared among all displays.
    u64 next_layer_id = 1;
    /// Id to use for the next buffer queue that is created, this counter is shared among all
    /// layers.
    u32 next_buffer_queue_id = 1;

    u32 swap_interval = 1;

    /// Event that handles screen composition.
    Core::Timing::EventType* composition_event;

    /// Core timing instance for registering/unregistering the composition event.
    Core::Timing::CoreTiming& core_timing;
};

} // namespace Service::NVFlinger
