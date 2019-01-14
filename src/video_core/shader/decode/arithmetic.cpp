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
using Tegra::Shader::SubOp;

u32 ShaderIR::DecodeArithmetic(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    Node op_a = GetRegister(instr.gpr8);

    Node op_b = [&]() -> Node {
        if (instr.is_b_imm) {
            return GetImmediate19(instr);
        } else if (instr.is_b_gpr) {
            return GetRegister(instr.gpr20);
        } else {
            return GetConstBuffer(instr.cbuf34.index, instr.cbuf34.offset);
        }
    }();

    switch (opcode->get().GetId()) {
    case OpCode::Id::MOV_C:
    case OpCode::Id::MOV_R: {
        // MOV does not have neither 'abs' nor 'neg' bits.
        SetRegister(bb, instr.gpr0, op_b);
        break;
    }
    case OpCode::Id::FMUL_C:
    case OpCode::Id::FMUL_R:
    case OpCode::Id::FMUL_IMM: {
        // FMUL does not have 'abs' bits and only the second operand has a 'neg' bit.
        UNIMPLEMENTED_IF_MSG(instr.fmul.tab5cb8_2 != 0, "FMUL tab5cb8_2({}) is not implemented",
                             instr.fmul.tab5cb8_2.Value());
        UNIMPLEMENTED_IF_MSG(
            instr.fmul.tab5c68_0 != 1, "FMUL tab5cb8_0({}) is not implemented",
            instr.fmul.tab5c68_0.Value()); // SMO typical sends 1 here which seems to be the default

        op_b = GetOperandAbsNegFloat(op_b, false, instr.fmul.negate_b);

        // TODO(Rodrigo): Should precise be used when there's a postfactor?
        Node value = Operation(OperationCode::FMul, PRECISE, op_a, op_b);

        if (instr.fmul.postfactor != 0) {
            auto postfactor = static_cast<s32>(instr.fmul.postfactor);

            // Postfactor encoded as 3-bit 1's complement in instruction, interpreted with below
            // logic.
            if (postfactor >= 4) {
                postfactor = 7 - postfactor;
            } else {
                postfactor = 0 - postfactor;
            }

            if (postfactor > 0) {
                value = Operation(OperationCode::FMul, NO_PRECISE, value,
                                  Immediate(static_cast<f32>(1 << postfactor)));
            } else {
                value = Operation(OperationCode::FDiv, NO_PRECISE, value,
                                  Immediate(static_cast<f32>(1 << -postfactor)));
            }
        }

        value = GetSaturatedFloat(value, instr.alu.saturate_d);

        SetInternalFlagsFromFloat(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::FADD_C:
    case OpCode::Id::FADD_R:
    case OpCode::Id::FADD_IMM: {
        op_a = GetOperandAbsNegFloat(op_a, instr.alu.abs_a, instr.alu.negate_a);
        op_b = GetOperandAbsNegFloat(op_b, instr.alu.abs_b, instr.alu.negate_b);

        Node value = Operation(OperationCode::FAdd, PRECISE, op_a, op_b);
        value = GetSaturatedFloat(value, instr.alu.saturate_d);

        SetInternalFlagsFromFloat(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::MUFU: {
        op_a = GetOperandAbsNegFloat(op_a, instr.alu.abs_a, instr.alu.negate_a);

        Node value = [&]() {
            switch (instr.sub_op) {
            case SubOp::Cos:
                return Operation(OperationCode::FCos, PRECISE, op_a);
            case SubOp::Sin:
                return Operation(OperationCode::FSin, PRECISE, op_a);
            case SubOp::Ex2:
                return Operation(OperationCode::FExp2, PRECISE, op_a);
            case SubOp::Lg2:
                return Operation(OperationCode::FLog2, PRECISE, op_a);
            case SubOp::Rcp:
                return Operation(OperationCode::FDiv, PRECISE, Immediate(1.0f), op_a);
            case SubOp::Rsq:
                return Operation(OperationCode::FInverseSqrt, PRECISE, op_a);
            case SubOp::Sqrt:
                return Operation(OperationCode::FSqrt, PRECISE, op_a);
            default:
                UNIMPLEMENTED_MSG("Unhandled MUFU sub op={0:x}",
                                  static_cast<unsigned>(instr.sub_op.Value()));
                return Immediate(0);
            }
        }();
        value = GetSaturatedFloat(value, instr.alu.saturate_d);

        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::FMNMX_C:
    case OpCode::Id::FMNMX_R:
    case OpCode::Id::FMNMX_IMM: {
        op_a = GetOperandAbsNegFloat(op_a, instr.alu.abs_a, instr.alu.negate_a);
        op_b = GetOperandAbsNegFloat(op_b, instr.alu.abs_b, instr.alu.negate_b);

        const Node condition = GetPredicate(instr.alu.fmnmx.pred, instr.alu.fmnmx.negate_pred != 0);

        const Node min = Operation(OperationCode::FMin, NO_PRECISE, op_a, op_b);
        const Node max = Operation(OperationCode::FMax, NO_PRECISE, op_a, op_b);
        const Node value = Operation(OperationCode::Select, NO_PRECISE, condition, min, max);

        SetInternalFlagsFromFloat(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::RRO_C:
    case OpCode::Id::RRO_R:
    case OpCode::Id::RRO_IMM: {
        // Currently RRO is only implemented as a register move.
        op_b = GetOperandAbsNegFloat(op_b, instr.alu.abs_b, instr.alu.negate_b);
        SetRegister(bb, instr.gpr0, op_b);
        LOG_WARNING(HW_GPU, "RRO instruction is incomplete");
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled arithmetic instruction: {}", opcode->get().GetName());
    }

    return pc;
}

} // namespace VideoCommon::Shader