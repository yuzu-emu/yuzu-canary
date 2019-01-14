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

u32 ShaderIR::DecodeBfe(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    UNIMPLEMENTED_IF(instr.bfe.negate_b);

    Node op_a = GetRegister(instr.gpr8);
    op_a = GetOperandAbsNegInteger(op_a, false, instr.bfe.negate_a, false);

    switch (opcode->get().GetId()) {
    case OpCode::Id::BFE_IMM: {
        UNIMPLEMENTED_IF_MSG(instr.generates_cc,
                             "Condition codes generation in BFE is not implemented");

        const Node inner_shift_imm = Immediate(static_cast<u32>(instr.bfe.GetLeftShiftValue()));
        const Node outer_shift_imm =
            Immediate(static_cast<u32>(instr.bfe.GetLeftShiftValue() + instr.bfe.shift_position));

        const Node inner_shift =
            Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, op_a, inner_shift_imm);
        const Node outer_shift =
            Operation(OperationCode::ILogicalShiftRight, NO_PRECISE, inner_shift, outer_shift_imm);

        SetInternalFlagsFromInteger(bb, outer_shift, instr.generates_cc);
        SetRegister(bb, instr.gpr0, outer_shift);
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled BFE instruction: {}", opcode->get().GetName());
    }

    return pc;
}

} // namespace VideoCommon::Shader