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

u32 ShaderIR::DecodeXmad(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    UNIMPLEMENTED_IF(instr.xmad.sign_a);
    UNIMPLEMENTED_IF(instr.xmad.sign_b);
    UNIMPLEMENTED_IF_MSG(instr.generates_cc,
                         "Condition codes generation in XMAD is not implemented");

    Node op_a = GetRegister(instr.gpr8);

    // TODO(bunnei): Needs to be fixed once op_a or op_b is signed
    UNIMPLEMENTED_IF(instr.xmad.sign_a != instr.xmad.sign_b);
    const bool is_signed_a = instr.xmad.sign_a == 1;
    const bool is_signed_b = instr.xmad.sign_b == 1;
    const bool is_signed_c = is_signed_a;

    auto [is_merge, op_b, op_c] = [&]() -> std::tuple<bool, Node, Node> {
        switch (opcode->get().GetId()) {
        case OpCode::Id::XMAD_CR:
            return {instr.xmad.merge_56, GetConstBuffer(instr.cbuf34.index, instr.cbuf34.offset),
                    GetRegister(instr.gpr39)};
        case OpCode::Id::XMAD_RR:
            return {instr.xmad.merge_37, GetRegister(instr.gpr20), GetRegister(instr.gpr39)};
        case OpCode::Id::XMAD_RC:
            return {false, GetRegister(instr.gpr39),
                    GetConstBuffer(instr.cbuf34.index, instr.cbuf34.offset)};
        case OpCode::Id::XMAD_IMM:
            return {instr.xmad.merge_37, Immediate(static_cast<u32>(instr.xmad.imm20_16)),
                    GetRegister(instr.gpr39)};
        }
        UNIMPLEMENTED_MSG("Unhandled XMAD instruction: {}", opcode->get().GetName());
        return {false, Immediate(0), Immediate(0)};
    }();

    op_a = BitfieldExtract(op_a, instr.xmad.high_a ? 16 : 0, 16);

    const Node original_b = op_b;
    op_b = BitfieldExtract(op_b, instr.xmad.high_b ? 16 : 0, 16);

    // TODO(Rodrigo): Use an appropiate sign for this operation
    Node product = Operation(OperationCode::IMul, NO_PRECISE, op_a, op_b);
    if (instr.xmad.product_shift_left) {
        product = Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, product, Immediate(16));
    }

    const Node original_c = op_c;
    op_c = [&]() {
        switch (instr.xmad.mode) {
        case Tegra::Shader::XmadMode::None:
            return original_c;
        case Tegra::Shader::XmadMode::CLo:
            return BitfieldExtract(original_c, 0, 16);
        case Tegra::Shader::XmadMode::CHi:
            return BitfieldExtract(original_c, 16, 16);
        case Tegra::Shader::XmadMode::CBcc: {
            const Node shifted_b = SignedOperation(OperationCode::ILogicalShiftLeft, is_signed_b,
                                                   NO_PRECISE, original_b, Immediate(16));
            return SignedOperation(OperationCode::IAdd, is_signed_c, NO_PRECISE, original_c,
                                   shifted_b);
        }
        default:
            UNIMPLEMENTED_MSG("Unhandled XMAD mode: {}", static_cast<u32>(instr.xmad.mode.Value()));
            return Immediate(0);
        }
    }();

    // TODO(Rodrigo): Use an appropiate sign for this operation
    Node sum = Operation(OperationCode::IAdd, product, op_c);
    if (is_merge) {
        const Node a = BitfieldExtract(sum, 0, 16);
        const Node b =
            Operation(OperationCode::ILogicalShiftLeft, NO_PRECISE, original_b, Immediate(16));
        sum = Operation(OperationCode::IBitwiseOr, NO_PRECISE, a, b);
    }

    SetInternalFlagsFromInteger(bb, sum, instr.generates_cc);
    SetRegister(bb, instr.gpr0, sum);

    return pc;
}

} // namespace VideoCommon::Shader