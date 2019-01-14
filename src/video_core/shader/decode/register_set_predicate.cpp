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

u32 ShaderIR::DecodeRegisterSetPredicate(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    UNIMPLEMENTED_IF(instr.r2p.mode != Tegra::Shader::R2pMode::Pr);

    const Node apply_mask = [&]() {
        switch (opcode->get().GetId()) {
        case OpCode::Id::R2P_IMM:
            return Immediate(static_cast<u32>(instr.r2p.immediate_mask));
        default:
            UNREACHABLE();
            return Immediate(static_cast<u32>(instr.r2p.immediate_mask));
        }
    }();
    const Node mask = GetRegister(instr.gpr8);
    const auto offset = static_cast<u32>(instr.r2p.byte) * 8;

    constexpr u32 programmable_preds = 7;
    for (u64 pred = 0; pred < programmable_preds; ++pred) {
        const auto shift = static_cast<u32>(pred);

        const Node apply_compare = BitfieldExtract(apply_mask, shift, 1);
        const Node condition =
            Operation(OperationCode::LogicalUNotEqual, apply_compare, Immediate(0));

        const Node value_compare = BitfieldExtract(mask, offset + shift, 1);
        const Node value = Operation(OperationCode::LogicalUNotEqual, value_compare, Immediate(0));

        const Node code = Operation(OperationCode::LogicalAssign, GetPredicate(pred), value);
        bb.push_back(Conditional(condition, {code}));
    }

    return pc;
}

} // namespace VideoCommon::Shader