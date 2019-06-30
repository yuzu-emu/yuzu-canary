#pragma once

#include <array>
#include "common/common_types.h"

namespace Service::Nvidia {

constexpr u32 MaxSyncPoints = 192;
constexpr u32 MaxNvEvents = 64;

struct Fence {
    s32 id;
    u32 value;
};

static_assert(sizeof(Fence) == 8, "Fence has wrong size");

struct MultiFence {
    u32 num_fences;
    std::array<Fence, 4> fences;
};

enum NvResult : u32 {
    Success = 0,
    BadParameter = 4,
    Timeout = 5,
    ResourceError = 15,
};

enum class EventState {
    Free = 0,
    Registered = 1,
    Waiting = 2,
    Busy = 3,
};

struct IoctlCtrl {
    bool fresh_call{true};
    bool must_delay{};
    s64 timeout{};
    s32 event_id{-1};
};

} // namespace Service::Nvidia
