// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <set>

#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/engines/shader_header.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

namespace {

/// Merges exit method of two parallel branches.
constexpr ExitMethod ParallelExit(ExitMethod a, ExitMethod b) {
    if (a == ExitMethod::Undetermined) {
        return b;
    }
    if (b == ExitMethod::Undetermined) {
        return a;
    }
    if (a == b) {
        return a;
    }
    return ExitMethod::Conditional;
}

/**
 * Returns whether the instruction at the specified offset is a 'sched' instruction.
 * Sched instructions always appear before a sequence of 3 instructions.
 */
constexpr bool IsSchedInstruction(u32 offset, u32 main_offset) {
    constexpr u32 SchedPeriod = 4;
    u32 absolute_offset = offset - main_offset;

    return (absolute_offset % SchedPeriod) == 0;
}

} // namespace

void ShaderIR::Decode() {
    std::memcpy(&header, program_code.data(), sizeof(Tegra::Shader::Header));

    std::set<u32> labels;
    const ExitMethod exit_method = Scan(main_offset, MAX_PROGRAM_LENGTH, labels);
    if (exit_method != ExitMethod::AlwaysEnd) {
        UNREACHABLE_MSG("Program does not always end");
    }

    if (labels.empty()) {
        basic_blocks.insert({main_offset, DecodeRange(main_offset, MAX_PROGRAM_LENGTH)});
        return;
    }

    labels.insert(main_offset);

    for (const u32 label : labels) {
        const auto next_it = labels.lower_bound(label + 1);
        const u32 next_label = next_it == labels.end() ? MAX_PROGRAM_LENGTH : *next_it;

        basic_blocks.insert({label, DecodeRange(label, next_label)});
    }
}

ExitMethod ShaderIR::Scan(u32 begin, u32 end, std::set<u32>& labels) {
    const auto [iter, inserted] =
        exit_method_map.emplace(std::make_pair(begin, end), ExitMethod::Undetermined);
    ExitMethod& exit_method = iter->second;
    if (!inserted)
        return exit_method;

    for (u32 offset = begin; offset != end && offset != MAX_PROGRAM_LENGTH; ++offset) {
        coverage_begin = std::min(coverage_begin, offset);
        coverage_end = std::max(coverage_end, offset + 1);

        const Instruction instr = {program_code[offset]};
        const auto opcode = OpCode::Decode(instr);
        if (!opcode)
            continue;
        switch (opcode->get().GetId()) {
        case OpCode::Id::EXIT: {
            // The EXIT instruction can be predicated, which means that the shader can conditionally
            // end on this instruction. We have to consider the case where the condition is not met
            // and check the exit method of that other basic block.
            using Tegra::Shader::Pred;
            if (instr.pred.pred_index == static_cast<u64>(Pred::UnusedIndex)) {
                return exit_method = ExitMethod::AlwaysEnd;
            } else {
                const ExitMethod not_met = Scan(offset + 1, end, labels);
                return exit_method = ParallelExit(ExitMethod::AlwaysEnd, not_met);
            }
        }
        case OpCode::Id::BRA: {
            const u32 target = offset + instr.bra.GetBranchTarget();
            labels.insert(target);
            const ExitMethod no_jmp = Scan(offset + 1, end, labels);
            const ExitMethod jmp = Scan(target, end, labels);
            return exit_method = ParallelExit(no_jmp, jmp);
        }
        case OpCode::Id::SSY:
        case OpCode::Id::PBK: {
            // The SSY and PBK use a similar encoding as the BRA instruction.
            UNIMPLEMENTED_IF_MSG(instr.bra.constant_buffer != 0,
                                 "Constant buffer branching is not supported");
            const u32 target = offset + instr.bra.GetBranchTarget();
            labels.insert(target);
            // Continue scanning for an exit method.
            break;
        }
        }
    }
    return exit_method = ExitMethod::AlwaysReturn;
}

NodeBlock ShaderIR::DecodeRange(u32 begin, u32 end) {
    NodeBlock basic_block;
    for (u32 pc = begin; pc < (begin > end ? MAX_PROGRAM_LENGTH : end);) {
        pc = DecodeInstr(basic_block, pc);
    }
    return basic_block;
}

u32 ShaderIR::DecodeInstr(NodeBlock& bb, u32 pc) {
    // Ignore sched instructions when generating code.
    if (IsSchedInstruction(pc, main_offset)) {
        return pc + 1;
    }

    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    // Decoding failure
    if (!opcode) {
        UNIMPLEMENTED_MSG("Unhandled instruction: {0:x}", instr.value);
        return pc + 1;
    }

    bb.push_back(
        Comment(fmt::format("{}: {} (0x{:016x})", pc, opcode->get().GetName(), instr.value)));

    using Tegra::Shader::Pred;
    UNIMPLEMENTED_IF_MSG(instr.pred.full_pred == Pred::NeverExecute,
                         "NeverExecute predicate not implemented");

    static const std::map<OpCode::Type, u32 (ShaderIR::*)(NodeBlock&, u32)> decoders = {
        {OpCode::Type::Arithmetic, &ShaderIR::DecodeArithmetic},
        {OpCode::Type::ArithmeticImmediate, &ShaderIR::DecodeArithmeticImmediate},
        {OpCode::Type::Bfe, &ShaderIR::DecodeBfe},
        {OpCode::Type::Bfi, &ShaderIR::DecodeBfi},
        {OpCode::Type::Shift, &ShaderIR::DecodeShift},
        {OpCode::Type::ArithmeticInteger, &ShaderIR::DecodeArithmeticInteger},
        {OpCode::Type::ArithmeticIntegerImmediate, &ShaderIR::DecodeArithmeticIntegerImmediate},
        {OpCode::Type::ArithmeticHalf, &ShaderIR::DecodeArithmeticHalf},
        {OpCode::Type::ArithmeticHalfImmediate, &ShaderIR::DecodeArithmeticHalfImmediate},
        {OpCode::Type::Ffma, &ShaderIR::DecodeFfma},
        {OpCode::Type::Hfma2, &ShaderIR::DecodeHfma2},
        {OpCode::Type::Conversion, &ShaderIR::DecodeConversion},
        {OpCode::Type::Memory, &ShaderIR::DecodeMemory},
        {OpCode::Type::Texture, &ShaderIR::DecodeTexture},
        {OpCode::Type::FloatSetPredicate, &ShaderIR::DecodeFloatSetPredicate},
        {OpCode::Type::IntegerSetPredicate, &ShaderIR::DecodeIntegerSetPredicate},
        {OpCode::Type::HalfSetPredicate, &ShaderIR::DecodeHalfSetPredicate},
        {OpCode::Type::PredicateSetRegister, &ShaderIR::DecodePredicateSetRegister},
        {OpCode::Type::PredicateSetPredicate, &ShaderIR::DecodePredicateSetPredicate},
        {OpCode::Type::RegisterSetPredicate, &ShaderIR::DecodeRegisterSetPredicate},
        {OpCode::Type::FloatSet, &ShaderIR::DecodeFloatSet},
        {OpCode::Type::IntegerSet, &ShaderIR::DecodeIntegerSet},
        {OpCode::Type::HalfSet, &ShaderIR::DecodeHalfSet},
        {OpCode::Type::Video, &ShaderIR::DecodeVideo},
        {OpCode::Type::Xmad, &ShaderIR::DecodeXmad},
    };

    std::vector<Node> tmp_block;
    if (const auto decoder = decoders.find(opcode->get().GetType()); decoder != decoders.end()) {
        pc = (this->*decoder->second)(tmp_block, pc);
    } else {
        pc = DecodeOther(tmp_block, pc);
    }

    // Some instructions (like SSY) don't have a predicate field, they are always unconditionally
    // executed.
    const bool can_be_predicated = OpCode::IsPredicatedInstruction(opcode->get().GetId());
    const auto pred_index = static_cast<u32>(instr.pred.pred_index);

    if (can_be_predicated && pred_index != static_cast<u32>(Pred::UnusedIndex)) {
        const Node conditional =
            Conditional(GetPredicate(pred_index, instr.negate_pred != 0), std::move(tmp_block));
        global_code.push_back(conditional);
        bb.push_back(conditional);
    } else {
        for (auto& node : tmp_block) {
            global_code.push_back(node);
            bb.push_back(node);
        }
    }

    return pc + 1;
}

} // namespace VideoCommon::Shader