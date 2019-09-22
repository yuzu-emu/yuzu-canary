// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <locale>
#include "common/hex_util.h"
#include "common/microprofile.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/sm/sm.h"
#include "core/memory/cheat_engine.h"

namespace Memory {

constexpr s64 CHEAT_ENGINE_TICKS = static_cast<s64>(Core::Timing::BASE_CLOCK_RATE / 12);
constexpr u32 KEYPAD_BITMASK = 0x3FFFFFF;

StandardVmCallbacks::StandardVmCallbacks(const Core::System& system,
                                         const CheatProcessMetadata& metadata)
    : system(system), metadata(metadata) {}

StandardVmCallbacks::~StandardVmCallbacks() = default;

void StandardVmCallbacks::MemoryRead(VAddr address, void* data, u64 size) {
    ReadBlock(SanitizeAddress(address), data, size);
}

void StandardVmCallbacks::MemoryWrite(VAddr address, const void* data, u64 size) {
    WriteBlock(SanitizeAddress(address), data, size);
}

u64 StandardVmCallbacks::HidKeysDown() {
    const auto applet_resource =
        system.ServiceManager().GetService<Service::HID::Hid>("hid")->GetAppletResource();
    if (applet_resource == nullptr) {
        LOG_WARNING(CheatEngine,
                    "Attempted to read input state, but applet resource is not initialized!");
        return false;
    }

    const auto press_state =
        applet_resource
            ->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad)
            .GetAndResetPressState();
    return press_state & KEYPAD_BITMASK;
}

void StandardVmCallbacks::DebugLog(u8 id, u64 value) {
    LOG_INFO(CheatEngine, "Cheat triggered DebugLog: ID '{:01X}' Value '{:016X}'", id, value);
}

void StandardVmCallbacks::CommandLog(std::string_view data) {
    LOG_DEBUG(CheatEngine, "[DmntCheatVm]: {}",
              data.back() == '\n' ? data.substr(0, data.size() - 1) : data);
}

VAddr StandardVmCallbacks::SanitizeAddress(VAddr in) const {
    if ((in < metadata.main_nso_extents.base ||
         in >= metadata.main_nso_extents.base + metadata.main_nso_extents.size) &&
        (in < metadata.heap_extents.base ||
         in >= metadata.heap_extents.base + metadata.heap_extents.size)) {
        LOG_ERROR(CheatEngine,
                  "Cheat attempting to access memory at invalid address={:016X}, if this "
                  "persists, "
                  "the cheat may be incorrect. However, this may be normal early in execution if "
                  "the game has not properly set up yet.",
                  in);
        return 0; ///< Invalid addresses will hard crash
    }

    return in;
}

CheatParser::~CheatParser() = default;

TextCheatParser::~TextCheatParser() = default;

namespace {
template <char match>
std::string_view ExtractName(std::string_view data, std::size_t start_index) {
    auto end_index = start_index;
    while (data[end_index] != match) {
        ++end_index;
        if (end_index > data.size() ||
            (end_index - start_index - 1) > sizeof(CheatDefinition::readable_name)) {
            return {};
        }
    }

    return data.substr(start_index, end_index - start_index);
}
} // Anonymous namespace

std::vector<CheatEntry> TextCheatParser::Parse(const Core::System& system,
                                               std::string_view data) const {
    std::vector<CheatEntry> out(1);
    std::optional<u64> current_entry = std::nullopt;

    for (std::size_t i = 0; i < data.size(); ++i) {
        if (::isspace(data[i])) {
            continue;
        }

        if (data[i] == '{') {
            current_entry = 0;

            if (out[*current_entry].definition.num_opcodes > 0) {
                return {};
            }

            const auto name = ExtractName<'}'>(data, i + 1);
            if (name.empty()) {
                return {};
            }

            std::memcpy(out[*current_entry].definition.readable_name.data(), name.data(),
                        std::min<std::size_t>(out[*current_entry].definition.readable_name.size(),
                                              name.size()));
            out[*current_entry]
                .definition.readable_name[out[*current_entry].definition.readable_name.size() - 1] =
                '\0';

            i += name.length() + 1;
        } else if (data[i] == '[') {
            current_entry = out.size();
            out.emplace_back();

            const auto name = ExtractName<']'>(data, i + 1);
            if (name.empty()) {
                return {};
            }

            std::memcpy(out[*current_entry].definition.readable_name.data(), name.data(),
                        std::min<std::size_t>(out[*current_entry].definition.readable_name.size(),
                                              name.size()));
            out[*current_entry]
                .definition.readable_name[out[*current_entry].definition.readable_name.size() - 1] =
                '\0';

            i += name.length() + 1;
        } else if (::isxdigit(data[i])) {
            if (!current_entry || out[*current_entry].definition.num_opcodes >=
                                      out[*current_entry].definition.opcodes.size()) {
                return {};
            }

            const auto hex = std::string(data.substr(i, 8));
            if (!std::all_of(hex.begin(), hex.end(), ::isxdigit)) {
                return {};
            }

            out[*current_entry].definition.opcodes[out[*current_entry].definition.num_opcodes++] =
                std::stoul(hex, nullptr, 0x10);

            i += 8;
        } else {
            return {};
        }
    }

    out[0].enabled = out[0].definition.num_opcodes > 0;
    out[0].cheat_id = 0;

    for (u32 i = 1; i < out.size(); ++i) {
        out[i].enabled = out[i].definition.num_opcodes > 0;
        out[i].cheat_id = i;
    }

    return out;
}

CheatEngine::CheatEngine(Core::System& system, std::vector<CheatEntry> cheats,
                         const std::array<u8, 0x20>& build_id)
    : system{system}, core_timing{system.CoreTiming()}, vm{std::make_unique<StandardVmCallbacks>(
                                                            system, metadata)},
      cheats(std::move(cheats)) {
    metadata.main_nso_build_id = build_id;
}

CheatEngine::~CheatEngine() {
    core_timing.UnscheduleEvent(event, 0);
}

void CheatEngine::Initialize() {
    event = core_timing.RegisterEvent(
        "CheatEngine::FrameCallback::" + Common::HexToString(metadata.main_nso_build_id),
        [this](u64 userdata, s64 cycles_late) { FrameCallback(userdata, cycles_late); });
    core_timing.ScheduleEvent(CHEAT_ENGINE_TICKS, event);

    metadata.process_id = system.CurrentProcess()->GetProcessID();
    metadata.title_id = system.CurrentProcess()->GetTitleID();

    const auto& vm_manager = system.CurrentProcess()->VMManager();
    metadata.heap_extents = {vm_manager.GetHeapRegionBaseAddress(), vm_manager.GetHeapRegionSize()};
    metadata.address_space_extents = {vm_manager.GetAddressSpaceBaseAddress(),
                                      vm_manager.GetAddressSpaceSize()};
    metadata.alias_extents = {vm_manager.GetMapRegionBaseAddress(), vm_manager.GetMapRegionSize()};

    is_pending_reload.exchange(true);
}

void CheatEngine::SetMainMemoryParameters(VAddr main_region_begin, u64 main_region_size) {
    metadata.main_nso_extents = {main_region_begin, main_region_size};
}

void CheatEngine::Reload(std::vector<CheatEntry> cheats) {
    this->cheats = std::move(cheats);
    is_pending_reload.exchange(true);
}

MICROPROFILE_DEFINE(Cheat_Engine, "Add-Ons", "Cheat Engine", MP_RGB(70, 200, 70));

void CheatEngine::FrameCallback(u64 userdata, s64 cycles_late) {
    if (is_pending_reload.exchange(false)) {
        vm.LoadProgram(cheats);
    }

    if (vm.GetProgramSize() == 0) {
        return;
    }

    MICROPROFILE_SCOPE(Cheat_Engine);

    vm.Execute(metadata);

    core_timing.ScheduleEvent(CHEAT_ENGINE_TICKS - cycles_late, event);
}

} // namespace Memory
