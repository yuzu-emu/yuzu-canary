// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <set>
#include <vector>
#include <queue>
#include "common/bit_field.h"
#include "common/common_types.h"

namespace Core::Timing {
struct EventType;
}

namespace FileSys {

enum class CodeType : u32 {
    // 0TMR00AA AAAAAAAA YYYYYYYY YYYYYYYY
    // Writes a T sized value Y to the address A added to the value of register R in memory domain M
    WriteImmediate = 0,

    // 1TMC00AA AAAAAAAA YYYYYYYY YYYYYYYY
    // Compares the T sized value Y to the value at address A in memory domain M using the
    // conditional function C. If success, continues execution. If failure, jumps to the matching
    // EndConditional statement.
    Conditional = 1,

    // 20000000
    // Terminates a Conditional or ConditionalInput block.
    EndConditional = 2,

    // 300R0000 VVVVVVVV
    // Starts looping V times, storing the current count in register R.
    // Loop block is terminated with a matching 310R0000.
    Loop = 3,

    // 400R0000 VVVVVVVV VVVVVVVV
    // Sets the value of register R to the value V.
    LoadImmediate = 4,

    // 5TMRI0AA AAAAAAAA
    // Sets the value of register R to the value of width T at address A in memory domain M, with
    // the current value of R added to the address if I == 1.
    LoadIndexed = 5,

    // 6T0RIFG0 VVVVVVVV VVVVVVVV
    // Writes the value V of width T to the memory address stored in register R. Adds the value of
    // register G to the final calculation if F is nonzero. Increments the value of register R by T
    // after operation if I is nonzero.
    StoreIndexed = 6,

    // 7T0RA000 VVVVVVVV
    // Performs the arithmetic operation A on the value in register R and the value V of width T,
    // storing the result in register R.
    RegisterArithmetic = 7,

    // 8KKKKKKK
    // Checks to see if any of the buttons defined by the bitmask K are pressed. If any are,
    // execution continues. If none are, execution skips to the next EndConditional command.
    ConditionalInput = 8,
};

enum class MemoryType : u32 {
    // Addressed relative to start of main NSO
    MainNSO = 0,

    // Addressed relative to start of heap
    Heap = 1,
};

enum class ArithmeticOp : u32 {
    Add = 0,
    Sub = 1,
    Mult = 2,
    LShift = 3,
    RShift = 4,
};

enum class ComparisonOp : u32 {
    GreaterThan = 1,
    GreaterThanEqual = 2,
    LessThan = 3,
    LessThanEqual = 4,
    Equal = 5,
    Inequal = 6,
};

union Cheat {
    std::array<u8, 16> raw;

    BitField<4, 4, CodeType> type;
    BitField<0, 4, u32> width; // Can be 1, 2, 4, or 8. Measured in bytes.
    BitField<0, 4, u32> end_of_loop;
    BitField<12, 4, MemoryType> memory_type;
    BitField<8, 4, u32> register_3;
    BitField<8, 4, ComparisonOp> comparison_op;
    BitField<20, 4, u32> load_from_register;
    BitField<20, 4, u32> increment_register;
    BitField<20, 4, ArithmeticOp> arithmetic_op;
    BitField<16, 4, u32> add_additional_register;
    BitField<28, 4, u32> register_6;

    u64 Address() const;
    u64 ValueWidth(u64 offset) const;
    u64 Value(u64 offset, u64 width) const;
    u32 KeypadValue() const;
};

class CheatParser;

// Represents a full collection of cheats for a game. The Execute function should be called every
// interval that all cheats should be executed. Clients should not directly instantiate this class
// (hence private constructor), they should instead receive an instance from CheatParser, which
// guarantees the list is always in an acceptable state.
class CheatList {
public:
    friend class CheatParser;

    using Block = std::vector<Cheat>;
    using ProgramSegment = std::vector<std::pair<std::string, Block>>;

    // (width in bytes, address, value)
    using MemoryWriter = void (*)(u32, VAddr, u64);
    // (width in bytes, address) -> value
    using MemoryReader = u64 (*)(u32, VAddr);

    void SetMemoryParameters(VAddr main_begin, VAddr heap_begin, VAddr main_end, VAddr heap_end,
                             MemoryWriter writer, MemoryReader reader);

    void Execute();

private:
    CheatList(ProgramSegment master, ProgramSegment standard);

    void ProcessBlockPairs(const Block& block);
    void ExecuteSingleCheat(const Cheat& cheat);

    void ExecuteBlock(const Block& block);

    bool EvaluateConditional(const Cheat& cheat) const;

    // Individual cheat operations
    void WriteImmediate(const Cheat& cheat);
    void BeginConditional(const Cheat& cheat);
    void EndConditional(const Cheat& cheat);
    void Loop(const Cheat& cheat);
    void LoadImmediate(const Cheat& cheat);
    void LoadIndexed(const Cheat& cheat);
    void StoreIndexed(const Cheat& cheat);
    void RegisterArithmetic(const Cheat& cheat);
    void BeginConditionalInput(const Cheat& cheat);

    VAddr SanitizeAddress(VAddr in) const;

    // Master Codes are defined as codes that cannot be disabled and are run prior to all
    // others.
    ProgramSegment master_list;
    // All other codes
    ProgramSegment standard_list;

    bool in_standard = false;

    // 16 (0x0-0xF) scratch registers that can be used by cheats
    std::array<u64, 16> scratch{};

    MemoryWriter writer = nullptr;
    MemoryReader reader = nullptr;

    u64 main_region_begin{};
    u64 heap_region_begin{};
    u64 main_region_end{};
    u64 heap_region_end{};

    u64 current_block{};
    // The current index of the cheat within the current Block
    u64 current_index{};

    // The 'stack' of the program. When a conditional or loop statement is encountered, its index is
    // pushed onto this queue. When a end block is encountered, the condition is checked.
    std::map<u64, u64> block_pairs;

    std::set<u64> encountered_loops;
};

// Intermediary class that parses a text file or other disk format for storing cheats into a
// CheatList object, that can be used for execution.
class CheatParser {
public:
    virtual ~CheatParser();

    virtual CheatList Parse(const std::vector<u8>& data) const = 0;

protected:
    CheatList MakeCheatList(CheatList::ProgramSegment master,
                            CheatList::ProgramSegment standard) const;
};

// CheatParser implementation that parses text files
class TextCheatParser final : public CheatParser {
public:
    ~TextCheatParser() override;

    CheatList Parse(const std::vector<u8>& data) const override;

private:
    std::array<u8, 16> ParseSingleLineCheat(const std::string& line) const;
};

// Class that encapsulates a CheatList and manages its interaction with memory and CoreTiming
class CheatEngine final {
public:
    CheatEngine(std::vector<CheatList> cheats, const std::string& build_id, VAddr code_region_start,
                VAddr code_region_end);
    ~CheatEngine();

private:
    void FrameCallback(u64 userdata, int cycles_late);

    Core::Timing::EventType* event;

    std::vector<CheatList> cheats;
};

} // namespace FileSys
