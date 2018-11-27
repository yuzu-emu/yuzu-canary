// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <queue>

#include "common/bit_field.h"
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace Tegra {

enum class SubmissionMode : u32 {
    IncreasingOld = 0,
    Increasing = 1,
    NonIncreasingOld = 2,
    NonIncreasing = 3,
    Inline = 4,
    IncreaseOnce = 5
};

struct CommandListHeader {
    union {
        u64 raw;
        BitField<0, 40, GPUVAddr> addr;
        BitField<41, 1, u64> is_non_main;
        BitField<42, 21, u64> size;
    };
};
static_assert(sizeof(CommandListHeader) == sizeof(u64), "CommandListHeader is incorrect size");

union CommandHeader {
    u32 argument;
    BitField<0, 13, u32> method;
    BitField<0, 24, u32> method_count_;
    BitField<13, 3, u32> subchannel;
    BitField<16, 13, u32> arg_count;
    BitField<16, 13, u32> method_count;
    BitField<29, 3, SubmissionMode> mode;
};
static_assert(std::is_standard_layout_v<CommandHeader>, "CommandHeader is not standard layout");
static_assert(sizeof(CommandHeader) == sizeof(u32), "CommandHeader has incorrect size!");

class GPU;

/**
 * The DmaPusher class implements DMA submission to FIFOs, providing an area of memory that the
 * emulated app fills with commands and tells PFIFO to process. The pushbuffers are then assembled
 * into a "command stream" consisting of 32-bit words that make up "commands".
 * See https://envytools.readthedocs.io/en/latest/hw/fifo/dma-pusher.html#fifo-dma-pusher for
 * details on this implementation.
 */
class DmaPusher {
public:
    explicit DmaPusher(GPU& gpu);
    ~DmaPusher();

    void Push(const CommandListHeader& command_list_header) {
        dma_pushbuffer.push(command_list_header);
    }

    void DispatchCalls();

private:
    bool Step();

    void SetState(const CommandHeader& command_header);

    void CallMethod(u32 argument) const;

    GPU& gpu;

    std::queue<CommandListHeader> dma_pushbuffer;

    struct DmaState {
        u32 method;            ///< Current method
        u32 subchannel;        ///< Current subchannel
        u32 method_count;      ///< Current method count
        u32 length_pending;    ///< Large NI command length pending
        bool non_incrementing; ///< Current command’s NI flag
    };

    DmaState dma_state{};
    bool dma_increment_once{};

    GPUVAddr dma_put{};   ///< pushbuffer current end address
    GPUVAddr dma_get{};   ///< pushbuffer current read address
    GPUVAddr dma_mget{};  ///< main pushbuffer last read address
    bool ib_enable{true}; ///< IB mode enabled
    bool non_main{};      ///< non-main pushbuffer active
};

} // namespace Tegra
