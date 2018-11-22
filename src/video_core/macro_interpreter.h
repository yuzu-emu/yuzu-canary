// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <optional>
#include <vector>

#include "common/bit_field.h"
#include "common/common_types.h"

namespace Tegra {
namespace Engines {
class Maxwell3D;
}

class MacroInterpreter final {
public:
    explicit MacroInterpreter(Engines::Maxwell3D& maxwell3d);

    /**
     * Executes the macro code with the specified input parameters.
     * @param offset Offset to start execution at.
     * @param parameters The parameters of the macro.
     */
    void Execute(u32 offset, std::vector<u32> parameters);

private:
    enum class Operation : u32 {
        ALU = 0,
        AddImmediate = 1,
        ExtractInsert = 2,
        ExtractShiftLeftImmediate = 3,
        ExtractShiftLeftRegister = 4,
        Read = 5,
        Unused = 6, // This operation doesn't seem to be a valid encoding.
        Branch = 7,
    };

    enum class ALUOperation : u32 {
        Add = 0,
        AddWithCarry = 1,
        Subtract = 2,
        SubtractWithBorrow = 3,
        // Operations 4-7 don't seem to be valid encodings.
        Xor = 8,
        Or = 9,
        And = 10,
        AndNot = 11,
        Nand = 12
    };

    enum class ResultOperation : u32 {
        IgnoreAndFetch = 0,
        Move = 1,
        MoveAndSetMethod = 2,
        FetchAndSend = 3,
        MoveAndSend = 4,
        FetchAndSetMethod = 5,
        MoveAndSetMethodFetchAndSend = 6,
        MoveAndSetMethodSend = 7
    };

    enum class BranchCondition : u32 {
        Zero = 0,
        NotZero = 1,
    };

    union Opcode {
        u32 raw;
        BitField<0, 3, Operation> operation;
        BitField<4, 3, ResultOperation> result_operation;
        BitField<4, 1, BranchCondition> branch_condition;
        BitField<5, 1, u32>
            branch_annul; // If set on a branch, then the branch doesn't have a delay slot.
        BitField<7, 1, u32> is_exit;
        BitField<8, 3, u32> dst;
        BitField<11, 3, u32> src_a;
        BitField<14, 3, u32> src_b;
        // The signed immediate overlaps the second source operand and the alu operation.
        BitField<14, 18, s32> immediate;

        BitField<17, 5, ALUOperation> alu_operation;

        // Bitfield instructions data
        BitField<17, 5, u32> bf_src_bit;
        BitField<22, 5, u32> bf_size;
        BitField<27, 5, u32> bf_dst_bit;

        u32 GetBitfieldMask() const {
            return (1 << bf_size) - 1;
        }

        s32 GetBranchTarget() const {
            return static_cast<s32>(immediate * sizeof(u32));
        }
    };

    union MethodAddress {
        u32 raw;
        BitField<0, 12, u32> address;
        BitField<12, 6, u32> increment;
    };

    /// Resets the execution engine state, zeroing registers, etc.
    void Reset();

    /**
     * Executes a single macro instruction located at the current program counter. Returns whether
     * the interpreter should keep running.
     * @param offset Offset to start execution at.
     * @param is_delay_slot Whether the current step is being executed due to a delay slot in a
     * previous instruction.
     */
    bool Step(u32 offset, bool is_delay_slot);

    /// Calculates the result of an ALU operation. src_a OP src_b;
    u32 GetALUResult(ALUOperation operation, u32 src_a, u32 src_b);

    /// Performs the result operation on the input result and stores it in the specified register
    /// (if necessary).
    void ProcessResult(ResultOperation operation, u32 reg, u32 result);

    /// Evaluates the branch condition and returns whether the branch should be taken or not.
    bool EvaluateBranchCondition(BranchCondition cond, u32 value) const;

    /// Reads an opcode at the current program counter location.
    Opcode GetOpcode(u32 offset) const;

    /// Returns the specified register's value. Register 0 is hardcoded to always return 0.
    u32 GetRegister(u32 register_id) const;

    /// Sets the register to the input value.
    void SetRegister(u32 register_id, u32 value);

    /// Sets the method address to use for the next Send instruction.
    void SetMethodAddress(u32 address);

    /// Calls a GPU Engine method with the input parameter.
    void Send(u32 value);

    /// Reads a GPU register located at the method address.
    u32 Read(u32 method) const;

    /// Returns the next parameter in the parameter queue.
    u32 FetchParameter();

    Engines::Maxwell3D& maxwell3d;

    u32 pc; ///< Current program counter
    std::optional<u32>
        delayed_pc; ///< Program counter to execute at after the delay slot is executed.

    static constexpr std::size_t NumMacroRegisters = 8;

    /// General purpose macro registers.
    std::array<u32, NumMacroRegisters> registers = {};

    /// Method address to use for the next Send instruction.
    MethodAddress method_address = {};

    /// Input parameters of the current macro.
    std::vector<u32> parameters;
    /// Index of the next parameter that will be fetched by the 'parm' instruction.
    u32 next_parameter_index = 0;

    bool carry_flag{};
};
} // namespace Tegra
