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
using Tegra::Shader::Pred;
using Tegra::Shader::VideoType;
using Tegra::Shader::VmadShr;

u32 ShaderIR::DecodeVideo(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    const Node op_a =
        GetVideoOperand(GetRegister(instr.gpr8), instr.video.is_byte_chunk_a, instr.video.signed_a,
                        instr.video.type_a, instr.video.byte_height_a);
    const Node op_b = [&]() {
        if (instr.video.use_register_b) {
            return GetVideoOperand(GetRegister(instr.gpr20), instr.video.is_byte_chunk_b,
                                   instr.video.signed_b, instr.video.type_b,
                                   instr.video.byte_height_b);
        }
        if (instr.video.signed_b) {
            const auto imm = static_cast<s16>(instr.alu.GetImm20_16());
            return Immediate(static_cast<u32>(imm));
        } else {
            return Immediate(instr.alu.GetImm20_16());
        }
    }();

    switch (opcode->get().GetId()) {
    case OpCode::Id::VMAD: {
        const bool result_signed = instr.video.signed_a == 1 || instr.video.signed_b == 1;
        const Node op_c = GetRegister(instr.gpr39);

        Node value = SignedOperation(OperationCode::IMul, result_signed, NO_PRECISE, op_a, op_b);
        value = SignedOperation(OperationCode::IAdd, result_signed, NO_PRECISE, value, op_c);

        if (instr.vmad.shr == VmadShr::Shr7 || instr.vmad.shr == VmadShr::Shr15) {
            const Node shift = Immediate(instr.vmad.shr == VmadShr::Shr7 ? 7 : 15);
            value =
                SignedOperation(OperationCode::IArithmeticShiftRight, result_signed, value, shift);
        }

        SetInternalFlagsFromInteger(bb, value, instr.generates_cc);
        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::VSETP: {
        // We can't use the constant predicate as destination.
        ASSERT(instr.vsetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

        const bool sign = instr.video.signed_a == 1 || instr.video.signed_b == 1;
        const Node first_pred = GetPredicateComparisonInteger(instr.vsetp.cond, sign, op_a, op_b);
        const Node second_pred = GetPredicate(instr.vsetp.pred39, false);

        const OperationCode combiner = GetPredicateCombiner(instr.vsetp.op);

        // Set the primary predicate to the result of Predicate OP SecondPredicate
        SetPredicate(bb, instr.vsetp.pred3, Operation(combiner, first_pred, second_pred));

        if (instr.vsetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
            // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
            // if enabled
            const Node negate_pred = Operation(OperationCode::LogicalNegate, first_pred);
            SetPredicate(bb, instr.vsetp.pred0, Operation(combiner, negate_pred, second_pred));
        }
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled video instruction: {}", opcode->get().GetName());
    }

    return pc;
}

Node ShaderIR::GetVideoOperand(Node op, bool is_chunk, bool is_signed,
                               Tegra::Shader::VideoType type, u64 byte_height) {
    if (!is_chunk) {
        return BitfieldExtract(op, static_cast<u32>(byte_height * 8), 8);
    }
    const Node zero = Immediate(0);

    switch (type) {
    case Tegra::Shader::VideoType::Size16_Low:
        return BitfieldExtract(op, 0, 16);
    case Tegra::Shader::VideoType::Size16_High:
        return BitfieldExtract(op, 16, 16);
    case Tegra::Shader::VideoType::Size32:
        // TODO(Rodrigo): From my hardware tests it becomes a bit "mad" when this type is used
        // (1 * 1 + 0 == 0x5b800000). Until a better explanation is found: abort.
        UNIMPLEMENTED();
        return zero;
    case Tegra::Shader::VideoType::Invalid:
        UNREACHABLE_MSG("Invalid instruction encoding");
        return zero;
    default:
        UNREACHABLE();
        return zero;
    }
}

} // namespace VideoCommon::Shader