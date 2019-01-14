// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

u32 ShaderIR::DecodeHalfSet(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    UNIMPLEMENTED_IF(instr.hset2.ftz != 0);

    // instr.hset2.type_a
    // instr.hset2.type_b
    Node op_a = GetRegister(instr.gpr8);
    Node op_b = [&]() {
        switch (opcode->get().GetId()) {
        case OpCode::Id::HSET2_R:
            return GetRegister(instr.gpr20);
        default:
            UNREACHABLE();
            return Immediate(0);
        }
    }();

    op_a = GetOperandAbsNegHalf(op_a, instr.hset2.abs_a, instr.hset2.negate_a);
    op_b = GetOperandAbsNegHalf(op_b, instr.hset2.abs_b, instr.hset2.negate_b);

    const Node second_pred = GetPredicate(instr.hset2.pred39, instr.hset2.neg_pred);

    MetaHalfArithmetic meta{false, {instr.hset2.type_a, instr.hset2.type_b}};
    const Node comparison_pair = GetPredicateComparisonHalf(instr.hset2.cond, meta, op_a, op_b);

    const OperationCode combiner = GetPredicateCombiner(instr.hset2.op);

    // HSET2 operates on each half float in the pack.
    std::array<Node, 2> values;
    for (u32 i = 0; i < 2; ++i) {
        const u32 raw_value = instr.hset2.bf ? 0x3c00 : 0xffff;
        const Node true_value = Immediate(raw_value << (i * 16));
        const Node false_value = Immediate(0);

        const Node comparison =
            Operation(OperationCode::LogicalPick2, comparison_pair, Immediate(i));
        const Node predicate = Operation(combiner, comparison, second_pred);

        values[i] =
            Operation(OperationCode::Select, NO_PRECISE, predicate, true_value, false_value);
    }

    const Node value = Operation(OperationCode::UBitwiseOr, NO_PRECISE, values[0], values[1]);
    SetRegister(bb, instr.gpr0, value);

    return pc;
}

} // namespace VideoCommon::Shader