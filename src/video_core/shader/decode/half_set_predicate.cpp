// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Pred;

u32 ShaderIR::DecodeHalfSetPredicate(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    UNIMPLEMENTED_IF(instr.hsetp2.ftz != 0);

    Node op_a = GetRegister(instr.gpr8);
    op_a = GetOperandAbsNegHalf(op_a, instr.hsetp2.abs_a, instr.hsetp2.negate_a);

    const Node op_b = [&]() {
        switch (opcode->get().GetId()) {
        case OpCode::Id::HSETP2_R:
            return GetOperandAbsNegHalf(GetRegister(instr.gpr20), instr.hsetp2.abs_a,
                                        instr.hsetp2.negate_b);
        default:
            UNREACHABLE();
            return Immediate(0);
        }
    }();

    // We can't use the constant predicate as destination.
    ASSERT(instr.hsetp2.pred3 != static_cast<u64>(Pred::UnusedIndex));

    const Node second_pred = GetPredicate(instr.hsetp2.pred39, instr.hsetp2.neg_pred != 0);

    const OperationCode combiner = GetPredicateCombiner(instr.hsetp2.op);
    const OperationCode pair_combiner =
        instr.hsetp2.h_and ? OperationCode::LogicalAll2 : OperationCode::LogicalAny2;

    MetaHalfArithmetic meta = {false, {instr.hsetp2.type_a, instr.hsetp2.type_b}};
    const Node comparison = GetPredicateComparisonHalf(instr.hsetp2.cond, meta, op_a, op_b);
    const Node first_pred = Operation(pair_combiner, comparison);

    // Set the primary predicate to the result of Predicate OP SecondPredicate
    const Node value = Operation(combiner, first_pred, second_pred);
    SetPredicate(bb, instr.hsetp2.pred3, value);

    if (instr.hsetp2.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
        // Set the secondary predicate to the result of !Predicate OP SecondPredicate, if enabled
        const Node negated_pred = Operation(OperationCode::LogicalNegate, first_pred);
        SetPredicate(bb, instr.hsetp2.pred0, Operation(combiner, negated_pred, second_pred));
    }

    return pc;
}

} // namespace VideoCommon::Shader