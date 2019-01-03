// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::IAdd3Height;
using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Pred;
using Tegra::Shader::Register;

u32 ShaderIR::DecodeArithmeticInteger(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    Node op_a = GetRegister(instr.gpr8);
    Node op_b = [&]() {
        if (instr.is_b_imm) {
            return Immediate(instr.alu.GetSignedImm20_20());
        } else if (instr.is_b_gpr) {
            return GetRegister(instr.gpr20);
        } else {
            return GetConstBuffer(instr.cbuf34.index, instr.cbuf34.offset);
        }
    }();

    switch (opcode->get().GetId()) {
    case OpCode::Id::IADD_C:
    case OpCode::Id::IADD_R:
    case OpCode::Id::IADD_IMM: {
        UNIMPLEMENTED_IF_MSG(instr.alu.saturate_d, "IADD saturation not implemented");

        op_a = GetOperandAbsNegInteger(op_a, false, instr.alu_integer.negate_a, true);
        op_b = GetOperandAbsNegInteger(op_b, false, instr.alu_integer.negate_b, true);

        const Node value = Operation(OperationCode::IAdd, PRECISE, op_a, op_b);

        SetInternalFlagsFromInteger(bb, value, instr.op_32.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::IADD3_C:
    case OpCode::Id::IADD3_R:
    case OpCode::Id::IADD3_IMM: {
        Node op_c = GetRegister(instr.gpr39);

        const auto ApplyHeight = [&](IAdd3Height height, Node value) {
            switch (height) {
            case IAdd3Height::None:
                return value;
            case IAdd3Height::LowerHalfWord:
                return BitfieldExtract(value, 0, 16);
            case IAdd3Height::UpperHalfWord:
                return BitfieldExtract(value, 16, 16);
            default:
                UNIMPLEMENTED_MSG("Unhandled IADD3 height: {}", static_cast<u32>(height));
                return Immediate(0);
            }
        };

        if (opcode->get().GetId() == OpCode::Id::IADD3_R) {
            op_a = ApplyHeight(instr.iadd3.height_a, op_a);
            op_b = ApplyHeight(instr.iadd3.height_b, op_b);
            op_c = ApplyHeight(instr.iadd3.height_c, op_c);
        }

        op_a = GetOperandAbsNegInteger(op_a, false, instr.iadd3.neg_a, true);
        op_b = GetOperandAbsNegInteger(op_b, false, instr.iadd3.neg_b, true);
        op_c = GetOperandAbsNegInteger(op_c, false, instr.iadd3.neg_c, true);

        const Node value = [&]() {
            const Node add_ab = Operation(OperationCode::IAdd, NO_PRECISE, op_a, op_b);
            if (opcode->get().GetId() != OpCode::Id::IADD3_R) {
                return Operation(OperationCode::IAdd, NO_PRECISE, add_ab, op_c);
            }
            const Node shifted = [&]() {
                switch (instr.iadd3.mode) {
                case Tegra::Shader::IAdd3Mode::RightShift:
                    // TODO(tech4me): According to
                    // https://envytools.readthedocs.io/en/latest/hw/graph/maxwell/cuda/int.html?highlight=iadd3
                    // The addition between op_a and op_b should be done in uint33, more
                    // investigation required
                    return Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, add_ab,
                                     Immediate(16));
                case Tegra::Shader::IAdd3Mode::LeftShift:
                    return Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, add_ab,
                                     Immediate(16));
                default:
                    return add_ab;
                }
            }();
            return Operation(OperationCode::IAdd, NO_PRECISE, shifted, op_c);
        }();

        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::ISCADD_C:
    case OpCode::Id::ISCADD_R:
    case OpCode::Id::ISCADD_IMM: {
        UNIMPLEMENTED_IF_MSG(instr.generates_cc,
                             "Condition codes generation in ISCADD is not implemented");

        op_a = GetOperandAbsNegInteger(op_a, false, instr.alu_integer.negate_a, true);
        op_b = GetOperandAbsNegInteger(op_b, false, instr.alu_integer.negate_b, true);

        const Node shift = Immediate(static_cast<u32>(instr.alu_integer.shift_amount));
        const Node shifted_a = Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, op_a, shift);
        const Node value = Operation(OperationCode::IAdd, NO_PRECISE, shifted_a, op_b);

        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::POPC_C:
    case OpCode::Id::POPC_R:
    case OpCode::Id::POPC_IMM: {
        if (instr.popc.invert) {
            op_b = Operation(OperationCode::IBitwiseNot, NO_PRECISE, op_b);
        }
        const Node value = Operation(OperationCode::IBitCount, PRECISE, op_b);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::SEL_C:
    case OpCode::Id::SEL_R:
    case OpCode::Id::SEL_IMM: {
        const Node condition = GetPredicate(instr.sel.pred, instr.sel.neg_pred != 0);
        const Node value = Operation(OperationCode::Select, PRECISE, condition, op_a, op_b);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::LOP_C:
    case OpCode::Id::LOP_R:
    case OpCode::Id::LOP_IMM: {
        if (instr.alu.lop.invert_a)
            op_a = Operation(OperationCode::IBitwiseNot, NO_PRECISE, op_a);
        if (instr.alu.lop.invert_b)
            op_b = Operation(OperationCode::IBitwiseNot, NO_PRECISE, op_b);

        WriteLogicOperation(bb, instr.gpr0, instr.alu.lop.operation, op_a, op_b,
                            instr.alu.lop.pred_result_mode, instr.alu.lop.pred48,
                            instr.generates_cc);
        break;
    }
    case OpCode::Id::LOP3_C:
    case OpCode::Id::LOP3_R:
    case OpCode::Id::LOP3_IMM: {
        const Node op_c = GetRegister(instr.gpr39);
        const Node lut = [&]() {
            if (opcode->get().GetId() == OpCode::Id::LOP3_R) {
                return Immediate(instr.alu.lop3.GetImmLut28());
            } else {
                return Immediate(instr.alu.lop3.GetImmLut48());
            }
        }();

        WriteLop3Instruction(bb, instr.gpr0, op_a, op_b, op_c, lut, instr.generates_cc);
        break;
    }
    case OpCode::Id::IMNMX_C:
    case OpCode::Id::IMNMX_R:
    case OpCode::Id::IMNMX_IMM: {
        UNIMPLEMENTED_IF(instr.imnmx.exchange != Tegra::Shader::IMinMaxExchange::None);

        const bool is_signed = instr.imnmx.is_signed;

        const Node condition = GetPredicate(instr.imnmx.pred, instr.imnmx.negate_pred != 0);
        const Node min = SignedOperation(OperationCode::IMin, is_signed, NO_PRECISE, op_a, op_b);
        const Node max = SignedOperation(OperationCode::IMax, is_signed, NO_PRECISE, op_a, op_b);
        const Node value = Operation(OperationCode::Select, NO_PRECISE, condition, min, max);

        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::LEA_R2:
    case OpCode::Id::LEA_R1:
    case OpCode::Id::LEA_IMM:
    case OpCode::Id::LEA_RZ:
    case OpCode::Id::LEA_HI: {
        const auto [op_a, op_b, op_c] = [&]() -> std::tuple<Node, Node, Node> {
            switch (opcode->get().GetId()) {
            case OpCode::Id::LEA_R2: {
                return {GetRegister(instr.gpr20), GetRegister(instr.gpr39),
                        Immediate(static_cast<u32>(instr.lea.r2.entry_a))};
            }

            case OpCode::Id::LEA_R1: {
                const bool neg = instr.lea.r1.neg != 0;
                return {GetOperandAbsNegInteger(GetRegister(instr.gpr8), false, neg, true),
                        GetRegister(instr.gpr20),
                        Immediate(static_cast<u32>(instr.lea.r1.entry_a))};
            }

            case OpCode::Id::LEA_IMM: {
                const bool neg = instr.lea.imm.neg != 0;
                return {Immediate(static_cast<u32>(instr.lea.imm.entry_a)),
                        GetOperandAbsNegInteger(GetRegister(instr.gpr8), false, neg, true),
                        Immediate(static_cast<u32>(instr.lea.imm.entry_b))};
            }

            case OpCode::Id::LEA_RZ: {
                const bool neg = instr.lea.rz.neg != 0;
                return {GetConstBuffer(instr.lea.rz.cb_index, instr.lea.rz.cb_offset),
                        GetOperandAbsNegInteger(GetRegister(instr.gpr8), false, neg, true),
                        Immediate(static_cast<u32>(instr.lea.rz.entry_a))};
            }

            case OpCode::Id::LEA_HI:
            default:
                UNIMPLEMENTED_MSG("Unhandled LEA subinstruction: {}", opcode->get().GetName());

                return {Immediate(static_cast<u32>(instr.lea.imm.entry_a)), GetRegister(instr.gpr8),
                        Immediate(static_cast<u32>(instr.lea.imm.entry_b))};
            }
        }();

        UNIMPLEMENTED_IF_MSG(instr.lea.pred48 != static_cast<u64>(Pred::UnusedIndex),
                             "Unhandled LEA Predicate");

        const Node shifted_c =
            Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, Immediate(1), op_c);
        const Node mul_bc = Operation(OperationCode::IMul, NO_PRECISE, op_b, shifted_c);
        const Node value = Operation(OperationCode::IAdd, NO_PRECISE, op_a, mul_bc);

        SetRegister(bb, instr.gpr0, value);

        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled ArithmeticInteger instruction: {}", opcode->get().GetName());
    }

    return pc;
}

void ShaderIR::WriteLop3Instruction(BasicBlock& bb, Register dest, Node op_a, Node op_b, Node op_c,
                                    Node imm_lut, bool sets_cc) {
    constexpr u32 lop_iterations = 32;
    const Node one = Immediate(1);
    const Node two = Immediate(2);

    Node value{};
    for (u32 i = 0; i < lop_iterations; ++i) {
        const Node shift_amount = Immediate(i);

        const Node a = Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, op_c, shift_amount);
        const Node pack_0 = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, a, one);

        const Node b = Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, op_b, shift_amount);
        const Node c = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, b, one);
        const Node pack_1 = Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, c, one);

        const Node d = Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, op_a, shift_amount);
        const Node e = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, d, one);
        const Node pack_2 = Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, e, two);

        const Node pack_01 = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, pack_0, pack_1);
        const Node pack_012 = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, pack_01, pack_2);

        const Node shifted_bit =
            Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, imm_lut, pack_012);
        const Node bit = Operation(OperationCode::IBitwiseAnd, NO_PRECISE, shifted_bit, one);

        const Node right =
            Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, bit, shift_amount);

        if (i > 0) {
            value = Operation(OperationCode::IBitwiseOr, NO_PRECISE, value, right);
        } else {
            value = right;
        }
    }

    SetInternalFlagsFromInteger(bb, value, sets_cc);
    SetRegister(bb, dest, value);
}

} // namespace VideoCommon::Shader