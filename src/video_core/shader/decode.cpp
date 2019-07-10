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
#include "video_core/shader/control_flow.h"
#include "video_core/shader/node_helper.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

namespace {

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

    disable_flow_stack = false;
    const auto info = ScanFlow(program_code, program_size, main_offset);
    if (info) {
        const auto& shader_info = *info;
        coverage_begin = shader_info.start;
        coverage_end = shader_info.end;
        if (shader_info.decompilable) {
            disable_flow_stack = true;
            const auto insert_block = ([this](NodeBlock& nodes, u32 label) {
                if (label == exit_branch) {
                    return;
                }
                basic_blocks.insert({label, nodes});
            });
            const auto& blocks = shader_info.blocks;
            NodeBlock current_block;
            u32 current_label = exit_branch;
            for (auto& block : blocks) {
                if (shader_info.labels.count(block.start) != 0) {
                    insert_block(current_block, current_label);
                    current_block.clear();
                    current_label = block.start;
                }
                if (!block.ignore_branch) {
                    DecodeRangeInner(current_block, block.start, block.end);
                    InsertControlFlow(current_block, block);
                } else {
                    DecodeRangeInner(current_block, block.start, block.end + 1);
                }
            }
            insert_block(current_block, current_label);
            return;
        }
        LOG_WARNING(HW_GPU, "Flow Stack Removing Failed! Falling back to old method");
        // we can't decompile it, fallback to standard method
        for (const auto& block : shader_info.blocks) {
            basic_blocks.insert({block.start, DecodeRange(block.start, block.end + 1)});
        }
        return;
    }
    LOG_WARNING(HW_GPU, "Flow Analysis Failed! Falling back to brute force compiling");

    // Now we need to deal with an undecompilable shader. We need to brute force
    // a shader that captures every position.
    coverage_begin = main_offset;
    const u32 shader_end = static_cast<u32>(program_size / sizeof(u64));
    coverage_end = shader_end;
    for (u32 label = main_offset; label < shader_end; label++) {
        basic_blocks.insert({label, DecodeRange(label, label + 1)});
    }
}

NodeBlock ShaderIR::DecodeRange(u32 begin, u32 end) {
    NodeBlock basic_block;
    DecodeRangeInner(basic_block, begin, end);
    return basic_block;
}

void ShaderIR::DecodeRangeInner(NodeBlock& bb, u32 begin, u32 end) {
    for (u32 pc = begin; pc < (begin > end ? MAX_PROGRAM_LENGTH : end);) {
        pc = DecodeInstr(bb, pc);
    }
}

void ShaderIR::InsertControlFlow(NodeBlock& bb, const ShaderBlock& block) {
    const auto apply_conditions = ([&](const Condition& cond, Node n) -> Node {
        Node result = n;
        if (cond.cc != ConditionCode::T) {
            result = Conditional(GetConditionCode(cond.cc), {result});
        }
        if (cond.predicate != Pred::UnusedIndex) {
            u32 pred = static_cast<u32>(cond.predicate);
            const bool is_neg = pred > 7;
            if (is_neg) {
                pred -= 8;
            }
            result = Conditional(GetPredicate(pred, is_neg), {result});
        }
        return result;
    });
    if (block.branch.address < 0) {
        if (block.branch.kills) {
            Node n = Operation(OperationCode::Discard);
            n = apply_conditions(block.branch.cond, n);
            bb.push_back(n);
            global_code.push_back(n);
            return;
        }
        Node n = Operation(OperationCode::Exit);
        n = apply_conditions(block.branch.cond, n);
        bb.push_back(n);
        global_code.push_back(n);
        return;
    }
    Node n = Operation(OperationCode::Branch, Immediate(block.branch.address));
    n = apply_conditions(block.branch.cond, n);
    bb.push_back(n);
    global_code.push_back(n);
}

u32 ShaderIR::DecodeInstr(NodeBlock& bb, u32 pc) {
    // Ignore sched instructions when generating code.
    if (IsSchedInstruction(pc, main_offset)) {
        return pc + 1;
    }

    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);
    const u32 nv_address = ConvertAddressToNvidiaSpace(pc);

    // Decoding failure
    if (!opcode) {
        UNIMPLEMENTED_MSG("Unhandled instruction: {0:x}", instr.value);
        bb.push_back(Comment(fmt::format("{:05x} Unimplemented Shader instruction (0x{:016x})",
                                         nv_address, instr.value)));
        return pc + 1;
    }

    bb.push_back(Comment(
        fmt::format("{:05x} {} (0x{:016x})", nv_address, opcode->get().GetName(), instr.value)));

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
        {OpCode::Type::Image, &ShaderIR::DecodeImage},
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
