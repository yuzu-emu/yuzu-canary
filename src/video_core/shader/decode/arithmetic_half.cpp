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

u32 ShaderIR::DecodeArithmeticHalf(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    if (opcode->get().GetId() == OpCode::Id::HADD2_C ||
        opcode->get().GetId() == OpCode::Id::HADD2_R) {
        UNIMPLEMENTED_IF(instr.alu_half.ftz != 0);
    }
    UNIMPLEMENTED_IF_MSG(instr.alu_half.saturate != 0, "Half float saturation not implemented");

    const bool negate_a =
        opcode->get().GetId() != OpCode::Id::HMUL2_R && instr.alu_half.negate_a != 0;
    const bool negate_b =
        opcode->get().GetId() != OpCode::Id::HMUL2_C && instr.alu_half.negate_b != 0;

    const Node op_a = GetOperandAbsNegHalf(GetRegister(instr.gpr8), instr.alu_half.abs_a, negate_a);

    // instr.alu_half.type_a

    Node op_b = [&]() {
        switch (opcode->get().GetId()) {
        case OpCode::Id::HADD2_C:
        case OpCode::Id::HMUL2_C:
            return GetConstBuffer(instr.cbuf34.index, instr.cbuf34.offset);
        case OpCode::Id::HADD2_R:
        case OpCode::Id::HMUL2_R:
            return GetRegister(instr.gpr20);
        default:
            UNREACHABLE();
            return Immediate(0);
        }
    }();
    op_b = GetOperandAbsNegHalf(op_b, instr.alu_half.abs_b, negate_b);

    Node value = [&]() {
        MetaHalfArithmetic meta{true, {instr.alu_half_imm.type_a, instr.alu_half.type_b}};
        switch (opcode->get().GetId()) {
        case OpCode::Id::HADD2_C:
        case OpCode::Id::HADD2_R:
            return Operation(OperationCode::HAdd, meta, op_a, op_b);
        case OpCode::Id::HMUL2_C:
        case OpCode::Id::HMUL2_R:
            return Operation(OperationCode::HMul, meta, op_a, op_b);
        default:
            UNIMPLEMENTED_MSG("Unhandled half float instruction: {}", opcode->get().GetName());
            return Immediate(0);
        }
    }();
    value = HalfMerge(GetRegister(instr.gpr0), value, instr.alu_half.merge);

    SetRegister(bb, instr.gpr0, value);

    return pc;
}

} // namespace VideoCommon::Shader