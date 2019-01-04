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

u32 ShaderIR::DecodeArithmeticHalfImmediate(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    if (opcode->get().GetId() == OpCode::Id::HADD2_IMM) {
        UNIMPLEMENTED_IF(instr.alu_half_imm.ftz != 0);
    } else {
        UNIMPLEMENTED_IF(instr.alu_half_imm.precision != Tegra::Shader::HalfPrecision::None);
    }
    UNIMPLEMENTED_IF_MSG(instr.alu_half_imm.saturate != 0,
                         "Half float immediate saturation not implemented");

    Node op_a = GetRegister(instr.gpr8);
    op_a = GetOperandAbsNegHalf(op_a, instr.alu_half_imm.abs_a, instr.alu_half_imm.negate_a);

    const Node op_b = UnpackHalfImmediate(instr, true);

    Node value = [&]() {
        MetaHalfArithmetic meta{true, {instr.alu_half_imm.type_a}};
        switch (opcode->get().GetId()) {
        case OpCode::Id::HADD2_IMM:
            return Operation(OperationCode::HAdd, meta, op_a, op_b);
        case OpCode::Id::HMUL2_IMM:
            return Operation(OperationCode::HMul, meta, op_a, op_b);
        default:
            UNREACHABLE();
            return Immediate(0);
        }
    }();
    value = HalfMerge(GetRegister(instr.gpr0), value, instr.alu_half_imm.merge);

    SetRegister(bb, instr.gpr0, value);

    return pc;
}

} // namespace VideoCommon::Shader