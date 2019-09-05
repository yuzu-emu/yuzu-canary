// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/node_helper.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

u32 ShaderIR::DecodeShift(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    Node op_a = GetRegister(instr.gpr8);
    Node op_b = [&]() {
        if (instr.is_b_imm) {
            return Immediate(instr.alu.GetSignedImm20_20());
        } else if (instr.is_b_gpr) {
            return GetRegister(instr.gpr20);
        } else {
            return GetConstBuffer(instr.cbuf34.index, instr.cbuf34.GetOffset());
        }
    }();

    switch (opcode->get().GetId()) {
    case OpCode::Id::SHR_C:
    case OpCode::Id::SHR_R:
    case OpCode::Id::SHR_IMM: {
        if (instr.shr.wrap) {
            op_b = Operation(OperationCode::UBitwiseAnd, std::move(op_b), Immediate(0x1f));
        } else {
            op_b = Operation(OperationCode::IMax, std::move(op_b), Immediate(0));
            op_b = Operation(OperationCode::IMin, std::move(op_b), Immediate(31));
        }

        Node value = SignedOperation(OperationCode::IArithmeticShiftRight, instr.shift.is_signed,
                                     std::move(op_a), std::move(op_b));
        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, std::move(value));
        break;
    }
    case OpCode::Id::SHL_C:
    case OpCode::Id::SHL_R:
    case OpCode::Id::SHL_IMM: {
        const Node value = Operation(OperationCode::ILogicalShiftLeft, op_a, op_b);
        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled shift instruction: {}", opcode->get().GetName());
    }

    return pc;
}

} // namespace VideoCommon::Shader
