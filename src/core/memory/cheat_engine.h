// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <vector>
#include "common/common_types.h"
#include "core/memory/dmnt_cheat_types.h"
#include "core/memory/dmnt_cheat_vm.h"

namespace Core {
class System;
}

namespace Core::Timing {
class CoreTiming;
struct EventType;
} // namespace Core::Timing

namespace Memory {

class StandardVmCallbacks : public DmntCheatVm::Callbacks {
public:
    StandardVmCallbacks(const Core::System& system, const CheatProcessMetadata& metadata);
    ~StandardVmCallbacks() override;

    void MemoryRead(VAddr address, void* data, u64 size) override;
    void MemoryWrite(VAddr address, const void* data, u64 size) override;
    u64 HidKeysDown() override;
    void DebugLog(u8 id, u64 value) override;
    void CommandLog(std::string_view data) override;

private:
    VAddr SanitizeAddress(VAddr address) const;

    const CheatProcessMetadata& metadata;
    const Core::System& system;
};

// Intermediary class that parses a text file or other disk format for storing cheats into a
// CheatList object, that can be used for execution.
class CheatParser {
public:
    virtual ~CheatParser();

    virtual std::vector<CheatEntry> Parse(const Core::System& system,
                                          std::string_view data) const = 0;
};

// CheatParser implementation that parses text files
class TextCheatParser final : public CheatParser {
public:
    ~TextCheatParser() override;

    std::vector<CheatEntry> Parse(const Core::System& system, std::string_view data) const override;
};

// Class that encapsulates a CheatList and manages its interaction with memory and CoreTiming
class CheatEngine final {
public:
    CheatEngine(Core::System& system_, std::vector<CheatEntry> cheats_,
                const std::array<u8, 0x20>& build_id);
    ~CheatEngine();

    void Initialize();
    void SetMainMemoryParameters(VAddr main_region_begin, u64 main_region_size);

    void Reload(std::vector<CheatEntry> cheats);

private:
    void FrameCallback(u64 userdata, s64 cycles_late);

    DmntCheatVm vm;
    CheatProcessMetadata metadata;

    std::vector<CheatEntry> cheats;
    std::atomic_bool is_pending_reload{false};

    Core::Timing::EventType* event{};
    Core::Timing::CoreTiming& core_timing;
    Core::System& system;
};

} // namespace Memory
