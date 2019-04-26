// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::HalfType;
using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

u32 ShaderIR::DecodeArithmeticHalf(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    if (opcode->get().GetId() == OpCode::Id::HADD2_C ||
        opcode->get().GetId() == OpCode::Id::HADD2_R) {
        if (instr.alu_half.ftz != 0) {
            LOG_WARNING(HW_GPU, "{} FTZ not implemented", opcode->get().GetName());
        }
    }

    const bool negate_a =
        opcode->get().GetId() != OpCode::Id::HMUL2_R && instr.alu_half.negate_a != 0;
    const bool negate_b =
        opcode->get().GetId() != OpCode::Id::HMUL2_C && instr.alu_half.negate_b != 0;

    Node op_a = UnpackHalfFloat(GetRegister(instr.gpr8), instr.alu_half.type_a);
    op_a = GetOperandAbsNegHalf(op_a, instr.alu_half.abs_a, negate_a);

    auto [type_b, op_b] = [&]() -> std::tuple<HalfType, Node> {
        switch (opcode->get().GetId()) {
        case OpCode::Id::HADD2_C:
        case OpCode::Id::HMUL2_C:
            return {HalfType::F32, GetConstBuffer(instr.cbuf34.index, instr.cbuf34.GetOffset())};
        case OpCode::Id::HADD2_R:
        case OpCode::Id::HMUL2_R:
            return {instr.alu_half.type_b, GetRegister(instr.gpr20)};
        default:
            UNREACHABLE();
            return {HalfType::F32, Immediate(0)};
        }
    }();
    op_b = UnpackHalfFloat(op_b, type_b);
    // redeclaration to avoid a bug in clang with reusing local bindings in lambdas
    Node op_b_alt = GetOperandAbsNegHalf(op_b, instr.alu_half.abs_b, negate_b);

    Node value = [&]() {
        switch (opcode->get().GetId()) {
        case OpCode::Id::HADD2_C:
        case OpCode::Id::HADD2_R:
            return Operation(OperationCode::HAdd, PRECISE, op_a, op_b_alt);
        case OpCode::Id::HMUL2_C:
        case OpCode::Id::HMUL2_R:
            return Operation(OperationCode::HMul, PRECISE, op_a, op_b_alt);
        default:
            UNIMPLEMENTED_MSG("Unhandled half float instruction: {}", opcode->get().GetName());
            return Immediate(0);
        }
    }();
    value = GetSaturatedHalfFloat(value, instr.alu_half.saturate);
    value = HalfMerge(GetRegister(instr.gpr0), value, instr.alu_half.merge);

    SetRegister(bb, instr.gpr0, value);

    return pc;
}

} // namespace VideoCommon::Shader
