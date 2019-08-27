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
    default:
        UNIMPLEMENTED_MSG("Unhandled warp instruction: {}", opcode->get().GetName());
        break;
    }

    return pc;
}

} // namespace VideoCommon::Shader
