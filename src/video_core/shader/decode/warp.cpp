// Copyright 2019 yuzu Emulator Project
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
using Tegra::Shader::Pred;
using Tegra::Shader::ShuffleOperation;
using Tegra::Shader::VoteOperation;

namespace {
OperationCode GetOperationCode(VoteOperation vote_op) {
    switch (vote_op) {
    case VoteOperation::All:
        return OperationCode::VoteAll;
    case VoteOperation::Any:
        return OperationCode::VoteAny;
    case VoteOperation::Eq:
        return OperationCode::VoteEqual;
    default:
        UNREACHABLE_MSG("Invalid vote operation={}", static_cast<u64>(vote_op));
        return OperationCode::VoteAll;
    }
}
} // Anonymous namespace

u32 ShaderIR::DecodeWarp(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::VOTE: {
        const Node value = GetPredicate(instr.vote.value, instr.vote.negate_value != 0);
        const Node active = Operation(OperationCode::BallotThread, value);
        const Node vote = Operation(GetOperationCode(instr.vote.operation), value);
        SetRegister(bb, instr.gpr0, active);
        SetPredicate(bb, instr.vote.dest_pred, vote);
        break;
    }
    case OpCode::Id::SHFL: {
        Node mask = instr.shfl.is_mask_imm ? Immediate(static_cast<u32>(instr.shfl.mask_imm))
                                           : GetRegister(instr.gpr39);
        Node width = [&] {
            // Convert the obscure SHFL mask back into GL_NV_shader_thread_shuffle's width. This has
            // been done reversing Nvidia's math. It won't work on all cases due to SHFL having
            // different parameters that don't properly map to GLSL's interface, but it should work
            // for cases emitted by Nvidia's compiler.
            if (instr.shfl.operation == ShuffleOperation::Up) {
                return Operation(
                    OperationCode::ILogicalShiftRight,
                    Operation(OperationCode::IAdd, std::move(mask), Immediate(-0x2000)),
                    Immediate(8));
            } else {
                return Operation(OperationCode::ILogicalShiftRight,
                                 Operation(OperationCode::IAdd, Immediate(0x201F),
                                           Operation(OperationCode::INegate, std::move(mask))),
                                 Immediate(8));
            }
        }();

        const auto [operation, in_range] = [instr]() -> std::pair<OperationCode, OperationCode> {
            switch (instr.shfl.operation) {
            case ShuffleOperation::Idx:
                return {OperationCode::ShuffleIndexed, OperationCode::InRangeShuffleIndexed};
            case ShuffleOperation::Up:
                return {OperationCode::ShuffleUp, OperationCode::InRangeShuffleUp};
            case ShuffleOperation::Down:
                return {OperationCode::ShuffleDown, OperationCode::InRangeShuffleDown};
            case ShuffleOperation::Bfly:
                return {OperationCode::ShuffleButterfly, OperationCode::InRangeShuffleButterfly};
            }
            UNREACHABLE_MSG("Invalid SHFL operation: {}",
                            static_cast<u64>(instr.shfl.operation.Value()));
            return {};
        }();

        // Setting the predicate before the register is intentional to avoid overwriting.
        Node index = instr.shfl.is_index_imm ? Immediate(static_cast<u32>(instr.shfl.index_imm))
                                             : GetRegister(instr.gpr20);
        SetPredicate(bb, instr.shfl.pred48, Operation(in_range, index, width));
        SetRegister(
            bb, instr.gpr0,
            Operation(operation, GetRegister(instr.gpr8), std::move(index), std::move(width)));
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled warp instruction: {}", opcode->get().GetName());
        break;
    }

    return pc;
}

} // namespace VideoCommon::Shader
