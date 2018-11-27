// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/memory.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"

namespace Tegra {

DmaPusher::DmaPusher(GPU& gpu) : gpu(gpu) {}

DmaPusher::~DmaPusher() = default;

void DmaPusher::DispatchCalls() {
    // On entering GPU code, assume all memory may be touched by the ARM core.
    gpu.Maxwell3D().dirty_flags.OnMemoryWrite();

    while (Core::System::GetInstance().IsPoweredOn()) {
        if (!Step()) {
            break;
        }
    }
}

bool DmaPusher::Step() {
    if (dma_get != dma_put) {
        // Push buffer non-empty, read a word
        const CommandHeader command_header{
            Memory::Read32(*gpu.MemoryManager().GpuToCpuAddress(dma_get))};

        dma_get += sizeof(u32);

        if (!non_main) {
            dma_mget = dma_get;
        }

        // now, see if we're in the middle of a command
        if (dma_state.length_pending) {
            // Second word of long non-inc methods command - method count
            dma_state.length_pending = 0;
            dma_state.method_count = command_header.method_count_;
        } else if (dma_state.method_count) {
            // Data word of methods command
            CallMethod(command_header.argument);

            if (!dma_state.non_incrementing) {
                dma_state.method++;
            }

            if (dma_increment_once) {
                dma_state.non_incrementing = true;
            }

            dma_state.method_count--;
        } else {
            // No command active - this is the first word of a new one
            switch (command_header.mode) {
            case SubmissionMode::Increasing:
                SetState(command_header);
                dma_state.non_incrementing = false;
                dma_increment_once = false;
                break;
            case SubmissionMode::NonIncreasing:
                SetState(command_header);
                dma_state.non_incrementing = true;
                dma_increment_once = false;
                break;
            case SubmissionMode::Inline:
                dma_state.method = command_header.method;
                dma_state.subchannel = command_header.subchannel;
                CallMethod(command_header.arg_count);
                dma_state.non_incrementing = true;
                dma_increment_once = false;
                break;
            case SubmissionMode::IncreaseOnce:
                SetState(command_header);
                dma_state.non_incrementing = false;
                dma_increment_once = true;
                break;
            }
        }
    } else if (ib_enable && !dma_pushbuffer.empty()) {
        // Current pushbuffer empty, but we have more IB entries to read
        const CommandListHeader& command_list_header{dma_pushbuffer.front()};
        dma_get = command_list_header.addr;
        dma_put = dma_get + command_list_header.size * sizeof(u32);
        non_main = command_list_header.is_non_main;
        dma_pushbuffer.pop();
    } else {
        // Otherwise, pushbuffer empty and IB empty or nonexistent - nothing to do
        return {};
    }

    return true;
}

void DmaPusher::SetState(const CommandHeader& command_header) {
    dma_state.method = command_header.method;
    dma_state.subchannel = command_header.subchannel;
    dma_state.method_count = command_header.method_count;
}

void DmaPusher::CallMethod(u32 argument) const {
    gpu.CallMethod({dma_state.method, argument, dma_state.subchannel, dma_state.method_count});
}

} // namespace Tegra
