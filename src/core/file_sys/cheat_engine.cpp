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
#include "core/file_sys/cheat_engine.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/hid/controllers/controller_base.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/sm/sm.h"

namespace FileSys {

constexpr u64 CHEAT_ENGINE_TICKS = Core::Timing::BASE_CLOCK_RATE / 60;
constexpr u32 KEYPAD_BITMASK = 0x3FFFFFF;

u64 Cheat::Address() const {
    u64 out;
    std::memcpy(&out, raw.data(), sizeof(u64));
    return Common::swap64(out) & 0xFFFFFFFFFF;
}

u64 Cheat::ValueWidth(u64 offset) const {
    return Value(offset, width);
}

u64 Cheat::Value(u64 offset, u64 width) const {
    u64 out;
    std::memcpy(&out, raw.data() + offset, sizeof(u64));
    out = Common::swap64(out);
    if (width == 8)
        return out;
    return out & ((1ull << (width * CHAR_BIT)) - 1);
}

u32 Cheat::KeypadValue() const {
    u32 out;
    std::memcpy(&out, raw.data(), sizeof(u32));
    return Common::swap32(out) & 0x0FFFFFFF;
}

void CheatList::SetMemoryParameters(VAddr main_begin, VAddr heap_begin, VAddr main_end,
                                    VAddr heap_end, MemoryWriter writer, MemoryReader reader) {
    this->main_region_begin = main_begin;
    this->main_region_end = main_end;
    this->heap_region_begin = heap_begin;
    this->heap_region_end = heap_end;
    this->writer = writer;
    this->reader = reader;
}

MICROPROFILE_DEFINE(Cheat_Engine, "Add-Ons", "Cheat Engine", MP_RGB(70, 200, 70));

void CheatList::Execute() {
    MICROPROFILE_SCOPE(Cheat_Engine);

    std::fill(scratch.begin(), scratch.end(), 0);
    in_standard = false;
    for (std::size_t i = 0; i < master_list.size(); ++i) {
        LOG_DEBUG(Common_Filesystem, "Executing block #{:08X} ({})", i, master_list[i].first);
        current_block = i;
        ExecuteBlock(master_list[i].second);
    }

    in_standard = true;
    for (std::size_t i = 0; i < standard_list.size(); ++i) {
        LOG_DEBUG(Common_Filesystem, "Executing block #{:08X} ({})", i, standard_list[i].first);
        current_block = i;
        ExecuteBlock(standard_list[i].second);
    }
}

CheatList::CheatList(ProgramSegment master, ProgramSegment standard)
    : master_list(master), standard_list(standard) {}

bool CheatList::EvaluateConditional(const Cheat& cheat) const {
    using ComparisonFunction = bool (*)(u64, u64);
    constexpr std::array<ComparisonFunction, 6> comparison_functions{
        [](u64 a, u64 b) { return a > b; },  [](u64 a, u64 b) { return a >= b; },
        [](u64 a, u64 b) { return a < b; },  [](u64 a, u64 b) { return a <= b; },
        [](u64 a, u64 b) { return a == b; }, [](u64 a, u64 b) { return a != b; },
    };

    if (cheat.type == CodeType::ConditionalInput) {
        const auto applet_resource = Core::System::GetInstance()
                                         .ServiceManager()
                                         .GetService<Service::HID::Hid>("hid")
                                         ->GetAppletResource();
        if (applet_resource == nullptr) {
            LOG_WARNING(
                Common_Filesystem,
                "Attempted to evaluate input conditional, but applet resource is not initialized!");
            return false;
        }

        const auto press_state =
            applet_resource
                ->GetController<Service::HID::Controller_NPad>(Service::HID::HidController::NPad)
                .GetAndResetPressState();
        return ((press_state & cheat.KeypadValue()) & KEYPAD_BITMASK) != 0;
    }

    ASSERT(cheat.type == CodeType::Conditional);

    const auto offset =
        cheat.memory_type == MemoryType::MainNSO ? main_region_begin : heap_region_begin;
    ASSERT(static_cast<u8>(cheat.comparison_op.Value()) < 6);
    auto* function = comparison_functions[static_cast<u8>(cheat.comparison_op.Value())];
    const auto addr = cheat.Address() + offset;

    return function(reader(cheat.width, SanitizeAddress(addr)), cheat.ValueWidth(8));
}

void CheatList::ProcessBlockPairs(const Block& block) {
    block_pairs.clear();

    u64 scope = 0;
    std::map<u64, u64> pairs;

    for (std::size_t i = 0; i < block.size(); ++i) {
        const auto& cheat = block[i];

        switch (cheat.type) {
        case CodeType::Conditional:
        case CodeType::ConditionalInput:
            pairs.insert_or_assign(scope, i);
            ++scope;
            break;
        case CodeType::EndConditional: {
            --scope;
            const auto idx = pairs.at(scope);
            block_pairs.insert_or_assign(idx, i);
            break;
        }
        case CodeType::Loop: {
            if (cheat.end_of_loop) {
                --scope;
                const auto idx = pairs.at(scope);
                block_pairs.insert_or_assign(idx, i);
            } else {
                pairs.insert_or_assign(scope, i);
                ++scope;
            }
            break;
        }
        }
    }
}

void CheatList::WriteImmediate(const Cheat& cheat) {
    const auto offset =
        cheat.memory_type == MemoryType::MainNSO ? main_region_begin : heap_region_begin;
    const auto& register_3 = scratch.at(cheat.register_3);

    const auto addr = cheat.Address() + offset + register_3;
    LOG_DEBUG(Common_Filesystem, "writing value={:016X} to addr={:016X}", addr,
              cheat.Value(8, cheat.width));
    writer(cheat.width, SanitizeAddress(addr), cheat.ValueWidth(8));
}

void CheatList::BeginConditional(const Cheat& cheat) {
    if (EvaluateConditional(cheat)) {
        return;
    }

    const auto iter = block_pairs.find(current_index);
    ASSERT(iter != block_pairs.end());
    current_index = iter->second - 1;
}

void CheatList::EndConditional(const Cheat& cheat) {
    LOG_DEBUG(Common_Filesystem, "Ending conditional block.");
}

void CheatList::Loop(const Cheat& cheat) {
    if (cheat.end_of_loop.Value())
        ASSERT(!cheat.end_of_loop.Value());

    auto& register_3 = scratch.at(cheat.register_3);
    const auto iter = block_pairs.find(current_index);
    ASSERT(iter != block_pairs.end());
    ASSERT(iter->first < iter->second);

    for (int i = cheat.Value(4, 4); i >= 0; --i) {
        register_3 = i;
        for (std::size_t c = iter->first + 1; c < iter->second; ++c) {
            current_index = c;
            ExecuteSingleCheat(
                (in_standard ? standard_list : master_list)[current_block].second[c]);
        }
    }

    current_index = iter->second;
}

void CheatList::LoadImmediate(const Cheat& cheat) {
    auto& register_3 = scratch.at(cheat.register_3);

    LOG_DEBUG(Common_Filesystem, "setting register={:01X} equal to value={:016X}", cheat.register_3,
              cheat.Value(4, 8));
    register_3 = cheat.Value(4, 8);
}

void CheatList::LoadIndexed(const Cheat& cheat) {
    const auto offset =
        cheat.memory_type == MemoryType::MainNSO ? main_region_begin : heap_region_begin;
    auto& register_3 = scratch.at(cheat.register_3);

    const auto addr = (cheat.load_from_register.Value() ? register_3 : offset) + cheat.Address();
    LOG_DEBUG(Common_Filesystem, "writing indexed value to register={:01X}, addr={:016X}",
              cheat.register_3, addr);
    register_3 = reader(cheat.width, SanitizeAddress(addr));
}

void CheatList::StoreIndexed(const Cheat& cheat) {
    const auto& register_3 = scratch.at(cheat.register_3);

    const auto addr =
        register_3 + (cheat.add_additional_register.Value() ? scratch.at(cheat.register_6) : 0);
    LOG_DEBUG(Common_Filesystem, "writing value={:016X} to addr={:016X}",
              cheat.Value(4, cheat.width), addr);
    writer(cheat.width, SanitizeAddress(addr), cheat.ValueWidth(4));
}

void CheatList::RegisterArithmetic(const Cheat& cheat) {
    using ArithmeticFunction = u64 (*)(u64, u64);
    constexpr std::array<ArithmeticFunction, 5> arithmetic_functions{
        [](u64 a, u64 b) { return a + b; },  [](u64 a, u64 b) { return a - b; },
        [](u64 a, u64 b) { return a * b; },  [](u64 a, u64 b) { return a << b; },
        [](u64 a, u64 b) { return a >> b; },
    };

    using ArithmeticOverflowCheck = bool (*)(u64, u64);
    constexpr std::array<ArithmeticOverflowCheck, 5> arithmetic_overflow_checks{
        [](u64 a, u64 b) { return a > (std::numeric_limits<u64>::max() - b); },       // a + b
        [](u64 a, u64 b) { return a > (std::numeric_limits<u64>::max() + b); },       // a - b
        [](u64 a, u64 b) { return a > (std::numeric_limits<u64>::max() / b); },       // a * b
        [](u64 a, u64 b) { return b >= 64 || (a & ~((1ull << (64 - b)) - 1)) != 0; }, // a << b
        [](u64 a, u64 b) { return b >= 64 || (a & ((1ull << b) - 1)) != 0; },         // a >> b
    };

    static_assert(sizeof(arithmetic_functions) == sizeof(arithmetic_overflow_checks),
                  "Missing or have extra arithmetic overflow checks compared to functions!");

    auto& register_3 = scratch.at(cheat.register_3);

    ASSERT(static_cast<u8>(cheat.arithmetic_op.Value()) < 5);
    auto* function = arithmetic_functions[static_cast<u8>(cheat.arithmetic_op.Value())];
    auto* overflow_function =
        arithmetic_overflow_checks[static_cast<u8>(cheat.arithmetic_op.Value())];
    LOG_DEBUG(Common_Filesystem, "performing arithmetic with register={:01X}, value={:016X}",
              cheat.register_3, cheat.ValueWidth(4));

    if (overflow_function(register_3, cheat.ValueWidth(4))) {
        LOG_WARNING(Common_Filesystem,
                    "overflow will occur when performing arithmetic operation={:02X} with operands "
                    "a={:016X}, b={:016X}!",
                    static_cast<u8>(cheat.arithmetic_op.Value()), register_3, cheat.ValueWidth(4));
    }

    register_3 = function(register_3, cheat.ValueWidth(4));
}

void CheatList::BeginConditionalInput(const Cheat& cheat) {
    if (EvaluateConditional(cheat))
        return;

    const auto iter = block_pairs.find(current_index);
    ASSERT(iter != block_pairs.end());
    current_index = iter->second - 1;
}

VAddr CheatList::SanitizeAddress(VAddr in) const {
    if ((in < main_region_begin || in >= main_region_end) &&
        (in < heap_region_begin || in >= heap_region_end)) {
        LOG_ERROR(Common_Filesystem,
                  "Cheat attempting to access memory at invalid address={:016X}, if this persists, "
                  "the cheat may be incorrect. However, this may be normal early in execution if "
                  "the game has not properly set up yet.",
                  in);
        return 0; ///< Invalid addresses will hard crash
    }

    return in;
}

void CheatList::ExecuteSingleCheat(const Cheat& cheat) {
    using CheatOperationFunction = void (CheatList::*)(const Cheat&);
    constexpr std::array<CheatOperationFunction, 9> cheat_operation_functions{
        &CheatList::WriteImmediate,        &CheatList::BeginConditional,
        &CheatList::EndConditional,        &CheatList::Loop,
        &CheatList::LoadImmediate,         &CheatList::LoadIndexed,
        &CheatList::StoreIndexed,          &CheatList::RegisterArithmetic,
        &CheatList::BeginConditionalInput,
    };

    const auto index = static_cast<u8>(cheat.type.Value());
    ASSERT(index < sizeof(cheat_operation_functions));
    const auto op = cheat_operation_functions[index];
    (this->*op)(cheat);
}

void CheatList::ExecuteBlock(const Block& block) {
    encountered_loops.clear();

    ProcessBlockPairs(block);
    for (std::size_t i = 0; i < block.size(); ++i) {
        current_index = i;
        ExecuteSingleCheat(block[i]);
        i = current_index;
    }
}

CheatParser::~CheatParser() = default;

CheatList CheatParser::MakeCheatList(CheatList::ProgramSegment master,
                                     CheatList::ProgramSegment standard) const {
    return {master, standard};
}

TextCheatParser::~TextCheatParser() = default;

CheatList TextCheatParser::Parse(const std::vector<u8>& data) const {
    std::stringstream ss;
    ss.write(reinterpret_cast<const char*>(data.data()), data.size());

    std::vector<std::string> lines;
    std::string stream_line;
    while (std::getline(ss, stream_line)) {
        // Remove a trailing \r
        if (!stream_line.empty() && stream_line.back() == '\r')
            stream_line.pop_back();
        lines.push_back(std::move(stream_line));
    }

    CheatList::ProgramSegment master_list;
    CheatList::ProgramSegment standard_list;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto line = lines[i];

        if (!line.empty() && (line[0] == '[' || line[0] == '{')) {
            const auto master = line[0] == '{';
            const auto begin = master ? line.find('{') : line.find('[');
            const auto end = master ? line.rfind('}') : line.rfind(']');

            ASSERT(begin != std::string::npos && end != std::string::npos);

            const std::string patch_name{line.begin() + begin + 1, line.begin() + end};
            CheatList::Block block{};

            while (i < lines.size() - 1) {
                line = lines[++i];
                if (!line.empty() && (line[0] == '[' || line[0] == '{')) {
                    --i;
                    break;
                }

                if (line.size() < 8)
                    continue;

                Cheat out{};
                out.raw = ParseSingleLineCheat(line);
                block.push_back(out);
            }

            (master ? master_list : standard_list).emplace_back(patch_name, block);
        }
    }

    return MakeCheatList(master_list, standard_list);
}

std::array<u8, 16> TextCheatParser::ParseSingleLineCheat(const std::string& line) const {
    std::array<u8, 16> out{};

    if (line.size() < 8)
        return out;

    const auto word1 = Common::HexStringToArray<sizeof(u32)>(std::string_view{line.data(), 8});
    std::memcpy(out.data(), word1.data(), sizeof(u32));

    if (line.size() < 17 || line[8] != ' ')
        return out;

    const auto word2 = Common::HexStringToArray<sizeof(u32)>(std::string_view{line.data() + 9, 8});
    std::memcpy(out.data() + sizeof(u32), word2.data(), sizeof(u32));

    if (line.size() < 26 || line[17] != ' ') {
        // Perform shifting in case value is truncated early.
        const auto type = static_cast<CodeType>((out[0] & 0xF0) >> 4);
        if (type == CodeType::Loop || type == CodeType::LoadImmediate ||
            type == CodeType::StoreIndexed || type == CodeType::RegisterArithmetic) {
            std::memcpy(out.data() + 8, out.data() + 4, sizeof(u32));
            std::memset(out.data() + 4, 0, sizeof(u32));
        }

        return out;
    }

    const auto word3 = Common::HexStringToArray<sizeof(u32)>(std::string_view{line.data() + 18, 8});
    std::memcpy(out.data() + 2 * sizeof(u32), word3.data(), sizeof(u32));

    if (line.size() < 35 || line[26] != ' ') {
        // Perform shifting in case value is truncated early.
        const auto type = static_cast<CodeType>((out[0] & 0xF0) >> 4);
        if (type == CodeType::WriteImmediate || type == CodeType::Conditional) {
            std::memcpy(out.data() + 12, out.data() + 8, sizeof(u32));
            std::memset(out.data() + 8, 0, sizeof(u32));
        }

        return out;
    }

    const auto word4 = Common::HexStringToArray<sizeof(u32)>(std::string_view{line.data() + 27, 8});
    std::memcpy(out.data() + 3 * sizeof(u32), word4.data(), sizeof(u32));

    return out;
}

u64 MemoryReadImpl(u32 width, VAddr addr) {
    switch (width) {
    case 1:
        return Memory::Read8(addr);
    case 2:
        return Memory::Read16(addr);
    case 4:
        return Memory::Read32(addr);
    case 8:
        return Memory::Read64(addr);
    default:
        UNREACHABLE();
        return 0;
    }
}

void MemoryWriteImpl(u32 width, VAddr addr, u64 value) {
    switch (width) {
    case 1:
        Memory::Write8(addr, static_cast<u8>(value));
        break;
    case 2:
        Memory::Write16(addr, static_cast<u16>(value));
        break;
    case 4:
        Memory::Write32(addr, static_cast<u32>(value));
        break;
    case 8:
        Memory::Write64(addr, value);
        break;
    default:
        UNREACHABLE();
    }
}

CheatEngine::CheatEngine(std::vector<CheatList> cheats, const std::string& build_id,
                         VAddr code_region_start, VAddr code_region_end)
    : cheats(std::move(cheats)) {
    auto& core_timing{Core::System::GetInstance().CoreTiming()};
    event = core_timing.RegisterEvent(
        "CheatEngine::FrameCallback::" + build_id,
        [this](u64 userdata, s64 cycles_late) { FrameCallback(userdata, cycles_late); });
    core_timing.ScheduleEvent(CHEAT_ENGINE_TICKS, event);

    const auto& vm_manager = Core::System::GetInstance().CurrentProcess()->VMManager();
    for (auto& list : this->cheats) {
        list.SetMemoryParameters(code_region_start, vm_manager.GetHeapRegionBaseAddress(),
                                 code_region_end, vm_manager.GetHeapRegionEndAddress(),
                                 &MemoryWriteImpl, &MemoryReadImpl);
    }
}

CheatEngine::~CheatEngine() {
    auto& core_timing{Core::System::GetInstance().CoreTiming()};
    core_timing.UnscheduleEvent(event, 0);
}

void CheatEngine::FrameCallback(u64 userdata, int cycles_late) {
    for (auto& list : cheats)
        list.Execute();

    auto& core_timing{Core::System::GetInstance().CoreTiming()};
    core_timing.ScheduleEvent(CHEAT_ENGINE_TICKS - cycles_late, event);
}

} // namespace FileSys
