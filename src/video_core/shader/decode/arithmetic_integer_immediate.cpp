// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::LogicOperation;
using Tegra::Shader::OpCode;
using Tegra::Shader::Pred;
using Tegra::Shader::PredicateResultMode;
using Tegra::Shader::Register;

u32 ShaderIR::DecodeArithmeticIntegerImmediate(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    Node op_a = GetRegister(instr.gpr8);
    Node op_b = Immediate(static_cast<s32>(instr.alu.imm20_32));

    switch (opcode->get().GetId()) {
    case OpCode::Id::IADD32I: {
        UNIMPLEMENTED_IF_MSG(instr.iadd32i.saturate, "IADD32I saturation is not implemented");

        op_a = GetOperandAbsNegInteger(op_a, false, instr.iadd32i.negate_a, true);

        const Node value = Operation(OperationCode::IAdd, PRECISE, op_a, op_b);

        SetInternalFlagsFromInteger(bb, value, instr.op_32.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::LOP32I: {
        if (instr.alu.lop32i.invert_a)
            op_a = Operation(OperationCode::IBitwiseNot, NO_PRECISE, op_a);

        if (instr.alu.lop32i.invert_b)
            op_b = Operation(OperationCode::IBitwiseNot, NO_PRECISE, op_b);

        WriteLogicOperation(bb, instr.gpr0, instr.alu.lop32i.operation, op_a, op_b,
                            PredicateResultMode::None, Pred::UnusedIndex, instr.op_32.generates_cc);
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled ArithmeticIntegerImmediate instruction: {}",
                          opcode->get().GetName());
    }

    return pc;
}

void ShaderIR::WriteLogicOperation(BasicBlock& bb, Register dest, LogicOperation logic_op,
                                   Node op_a, Node op_b, PredicateResultMode predicate_mode,
                                   Pred predicate, bool sets_cc) {
    const Node result = [&]() {
        switch (logic_op) {
        case LogicOperation::And:
            return Operation(OperationCode::IBitwiseAnd, PRECISE, op_a, op_b);
        case LogicOperation::Or:
            return Operation(OperationCode::IBitwiseOr, PRECISE, op_a, op_b);
        case LogicOperation::Xor:
            return Operation(OperationCode::IBitwiseXor, PRECISE, op_a, op_b);
        case LogicOperation::PassB:
            return op_b;
        default:
            UNIMPLEMENTED_MSG("Unimplemented logic operation={}", static_cast<u32>(logic_op));
            return Immediate(0);
        }
    }();

    SetInternalFlagsFromInteger(bb, result, sets_cc);
    SetRegister(bb, dest, result);

    // Write the predicate value depending on the predicate mode.
    switch (predicate_mode) {
    case PredicateResultMode::None:
        // Do nothing.
        return;
    case PredicateResultMode::NotZero: {
        // Set the predicate to true if the result is not zero.
        const Node compare = Operation(OperationCode::LogicalINotEqual, result, Immediate(0));
        SetPredicate(bb, static_cast<u64>(predicate), compare);
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unimplemented predicate result mode: {}",
                          static_cast<u32>(predicate_mode));
    }
}

} // namespace VideoCommon::Shader