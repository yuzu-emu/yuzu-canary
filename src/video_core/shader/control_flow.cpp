// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <map>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/shader/control_flow.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

constexpr s32 unassigned_branch = -2;

struct Query {
    u32 address{};
    std::stack<u32> ssy_stack{};
    std::stack<u32> pbk_stack{};
};

struct BlockStack {
    BlockStack() = default;
    BlockStack(const BlockStack& b) = default;
    BlockStack(const Query& q) : ssy_stack{q.ssy_stack}, pbk_stack{q.pbk_stack} {}
    std::stack<u32> ssy_stack{};
    std::stack<u32> pbk_stack{};
};

struct BlockBranchInfo {
    Condition condition{};
    s32 address{exit_branch};
    bool kill{};
    bool is_sync{};
    bool is_brk{};
    bool ignore{};
};

struct BlockInfo {
    u32 start{};
    u32 end{};
    bool visited{};
    BlockBranchInfo branch{};

    bool IsInside(const u32 address) const {
        return start <= address && address <= end;
    }
};

struct CFGRebuildState {
    explicit CFGRebuildState(const ProgramCode& program_code, const std::size_t program_size,
                             const u32 start)
        : program_code{program_code}, program_size{program_size}, start{start} {}

    u32 start{};
    std::vector<BlockInfo> block_info{};
    std::list<u32> inspect_queries{};
    std::list<Query> queries{};
    std::unordered_map<u32, u32> registered{};
    std::unordered_set<u32> labels{};
    std::map<u32, u32> ssy_labels{};
    std::map<u32, u32> pbk_labels{};
    std::unordered_map<u32, BlockStack> stacks{};
    const ProgramCode& program_code;
    const std::size_t program_size;
};

enum class BlockCollision : u32 { None, Found, Inside };

std::pair<BlockCollision, u32> TryGetBlock(CFGRebuildState& state, u32 address) {
    const auto& blocks = state.block_info;
    for (u32 index = 0; index < blocks.size(); index++) {
        if (blocks[index].start == address) {
            return {BlockCollision::Found, index};
        }
        if (blocks[index].IsInside(address)) {
            return {BlockCollision::Inside, index};
        }
    }
    return {BlockCollision::None, -1};
}

struct ParseInfo {
    BlockBranchInfo branch_info{};
    u32 end_address{};
};

BlockInfo& CreateBlockInfo(CFGRebuildState& state, u32 start, u32 end) {
    auto& it = state.block_info.emplace_back();
    it.start = start;
    it.end = end;
    const u32 index = static_cast<u32>(state.block_info.size() - 1);
    state.registered.insert({start, index});
    return it;
}

Pred GetPredicate(u32 index, bool negated) {
    return static_cast<Pred>(index + (negated ? 8 : 0));
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

enum class ParseResult : u32 {
    ControlCaught,
    BlockEnd,
    AbnormalFlow,
};

std::pair<ParseResult, ParseInfo> ParseCode(CFGRebuildState& state, u32 address) {
    u32 offset = static_cast<u32>(address);
    const u32 end_address = static_cast<u32>(state.program_size / sizeof(Instruction));
    ParseInfo parse_info{};

    const auto insert_label = [](CFGRebuildState& state, u32 address) {
        const auto pair = state.labels.emplace(address);
        if (pair.second) {
            state.inspect_queries.push_back(address);
        }
    };

    while (true) {
        if (offset >= end_address) {
            // ASSERT_OR_EXECUTE can't be used, as it ignores the break
            ASSERT_MSG(false, "Shader passed the current limit!");
            parse_info.branch_info.address = exit_branch;
            parse_info.branch_info.ignore = false;
            break;
        }
        if (state.registered.count(offset) != 0) {
            parse_info.branch_info.address = offset;
            parse_info.branch_info.ignore = true;
            break;
        }
        if (IsSchedInstruction(offset, state.start)) {
            offset++;
            continue;
        }
        const Instruction instr = {state.program_code[offset]};
        const auto opcode = OpCode::Decode(instr);
        if (!opcode || opcode->get().GetType() != OpCode::Type::Flow) {
            offset++;
            continue;
        }

        switch (opcode->get().GetId()) {
        case OpCode::Id::EXIT: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            parse_info.branch_info.condition.predicate =
                GetPredicate(pred_index, instr.negate_pred != 0);
            if (parse_info.branch_info.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            parse_info.branch_info.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            parse_info.branch_info.address = exit_branch;
            parse_info.branch_info.kill = false;
            parse_info.branch_info.is_sync = false;
            parse_info.branch_info.is_brk = false;
            parse_info.branch_info.ignore = false;
            parse_info.end_address = offset;

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::BRA: {
            if (instr.bra.constant_buffer != 0) {
                return {ParseResult::AbnormalFlow, parse_info};
            }
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            parse_info.branch_info.condition.predicate =
                GetPredicate(pred_index, instr.negate_pred != 0);
            if (parse_info.branch_info.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            parse_info.branch_info.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            const u32 branch_offset = offset + instr.bra.GetBranchTarget();
            if (branch_offset == 0) {
                parse_info.branch_info.address = exit_branch;
            } else {
                parse_info.branch_info.address = branch_offset;
            }
            insert_label(state, branch_offset);
            parse_info.branch_info.kill = false;
            parse_info.branch_info.is_sync = false;
            parse_info.branch_info.is_brk = false;
            parse_info.branch_info.ignore = false;
            parse_info.end_address = offset;

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::SYNC: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            parse_info.branch_info.condition.predicate =
                GetPredicate(pred_index, instr.negate_pred != 0);
            if (parse_info.branch_info.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            parse_info.branch_info.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            parse_info.branch_info.address = unassigned_branch;
            parse_info.branch_info.kill = false;
            parse_info.branch_info.is_sync = true;
            parse_info.branch_info.is_brk = false;
            parse_info.branch_info.ignore = false;
            parse_info.end_address = offset;

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::BRK: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            parse_info.branch_info.condition.predicate =
                GetPredicate(pred_index, instr.negate_pred != 0);
            if (parse_info.branch_info.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            parse_info.branch_info.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            parse_info.branch_info.address = unassigned_branch;
            parse_info.branch_info.kill = false;
            parse_info.branch_info.is_sync = false;
            parse_info.branch_info.is_brk = true;
            parse_info.branch_info.ignore = false;
            parse_info.end_address = offset;

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::KIL: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            parse_info.branch_info.condition.predicate =
                GetPredicate(pred_index, instr.negate_pred != 0);
            if (parse_info.branch_info.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            parse_info.branch_info.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            parse_info.branch_info.address = exit_branch;
            parse_info.branch_info.kill = true;
            parse_info.branch_info.is_sync = false;
            parse_info.branch_info.is_brk = false;
            parse_info.branch_info.ignore = false;
            parse_info.end_address = offset;

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::SSY: {
            const u32 target = offset + instr.bra.GetBranchTarget();
            insert_label(state, target);
            state.ssy_labels.emplace(offset, target);
            break;
        }
        case OpCode::Id::PBK: {
            const u32 target = offset + instr.bra.GetBranchTarget();
            insert_label(state, target);
            state.pbk_labels.emplace(offset, target);
            break;
        }
        case OpCode::Id::BRX: {
            return {ParseResult::AbnormalFlow, parse_info};
        }
        default:
            break;
        }

        offset++;
    }
    parse_info.branch_info.kill = false;
    parse_info.branch_info.is_sync = false;
    parse_info.branch_info.is_brk = false;
    parse_info.end_address = offset - 1;
    return {ParseResult::BlockEnd, parse_info};
}

bool TryInspectAddress(CFGRebuildState& state) {
    if (state.inspect_queries.empty()) {
        return false;
    }

    const u32 address = state.inspect_queries.front();
    state.inspect_queries.pop_front();
    const auto [result, block_index] = TryGetBlock(state, address);
    switch (result) {
    case BlockCollision::Found: {
        return true;
    }
    case BlockCollision::Inside: {
        // This case is the tricky one:
        // We need to Split the block in 2 sepparate blocks
        const u32 end = state.block_info[block_index].end;
        BlockInfo& new_block = CreateBlockInfo(state, address, end);
        BlockInfo& current_block = state.block_info[block_index];
        current_block.end = address - 1;
        new_block.branch = current_block.branch;
        BlockBranchInfo forward_branch{};
        forward_branch.address = address;
        forward_branch.ignore = true;
        current_block.branch = forward_branch;
        return true;
    }
    default:
        break;
    }
    const auto [parse_result, parse_info] = ParseCode(state, address);
    if (parse_result == ParseResult::AbnormalFlow) {
        // if it's AbnormalFlow, we end it as false, ending the CFG reconstruction
        return false;
    }

    BlockInfo& block_info = CreateBlockInfo(state, address, parse_info.end_address);
    block_info.branch = parse_info.branch_info;
    if (parse_info.branch_info.condition.IsUnconditional()) {
        return true;
    }

    const u32 fallthrough_address = parse_info.end_address + 1;
    state.inspect_queries.push_front(fallthrough_address);
    return true;
}

bool TryQuery(CFGRebuildState& state) {
    const auto gather_labels = [](std::stack<u32>& cc, std::map<u32, u32>& labels,
                                  BlockInfo& block) {
        auto gather_start = labels.lower_bound(block.start);
        const auto gather_end = labels.upper_bound(block.end);
        while (gather_start != gather_end) {
            cc.push(gather_start->second);
            gather_start++;
        }
    };
    if (state.queries.empty()) {
        return false;
    }
    Query& q = state.queries.front();
    const u32 block_index = state.registered[q.address];
    BlockInfo& block = state.block_info[block_index];
    // If the block is visted, check if the stacks match, else gather the ssy/pbk
    // labels into the current stack and look if the branch at the end of the block
    // consumes a label. Schedule new queries accordingly
    if (block.visited) {
        BlockStack& stack = state.stacks[q.address];
        const bool all_okay = (stack.ssy_stack.size() == 0 || q.ssy_stack == stack.ssy_stack) &&
                              (stack.pbk_stack.size() == 0 || q.pbk_stack == stack.pbk_stack);
        state.queries.pop_front();
        return all_okay;
    }
    block.visited = true;
    state.stacks[q.address] = BlockStack{q};
    Query q2(q);
    state.queries.pop_front();
    gather_labels(q2.ssy_stack, state.ssy_labels, block);
    gather_labels(q2.pbk_stack, state.pbk_labels, block);
    if (!block.branch.condition.IsUnconditional()) {
        q2.address = block.end + 1;
        state.queries.push_back(q2);
    }
    Query conditional_query{q2};
    if (block.branch.is_sync) {
        if (block.branch.address == unassigned_branch) {
            block.branch.address = conditional_query.ssy_stack.top();
        }
        conditional_query.ssy_stack.pop();
    }
    if (block.branch.is_brk) {
        if (block.branch.address == unassigned_branch) {
            block.branch.address = conditional_query.pbk_stack.top();
        }
        conditional_query.pbk_stack.pop();
    }
    conditional_query.address = block.branch.address;
    state.queries.push_back(conditional_query);
    return true;
}

std::optional<ShaderCharacteristics> ScanFlow(const ProgramCode& program_code, u32 program_size,
                                              u32 start_address) {
    CFGRebuildState state{program_code, program_size, start_address};
    // Inspect Code and generate blocks
    state.labels.clear();
    state.labels.emplace(start_address);
    state.inspect_queries.push_back(state.start);
    while (!state.inspect_queries.empty()) {
        if (!TryInspectAddress(state)) {
            return {};
        }
    }
    // Decompile Stacks
    Query start_query{};
    start_query.address = state.start;
    state.queries.push_back(start_query);
    bool decompiled = true;
    while (!state.queries.empty()) {
        if (!TryQuery(state)) {
            decompiled = false;
            break;
        }
    }
    // Sort and organize results
    std::sort(state.block_info.begin(), state.block_info.end(),
              [](const BlockInfo& a, const BlockInfo& b) -> bool { return a.start < b.start; });
    ShaderCharacteristics result_out{};
    result_out.decompilable = decompiled;
    result_out.start = start_address;
    result_out.end = start_address;
    for (auto& block : state.block_info) {
        ShaderBlock new_block{};
        new_block.start = block.start;
        new_block.end = block.end;
        new_block.ignore_branch = block.branch.ignore;
        if (!new_block.ignore_branch) {
            new_block.branch.cond = block.branch.condition;
            new_block.branch.kills = block.branch.kill;
            new_block.branch.address = block.branch.address;
        }
        result_out.end = std::max(result_out.end, block.end);
        result_out.blocks.push_back(new_block);
    }
    if (result_out.decompilable) {
        result_out.labels = std::move(state.labels);
        return {result_out};
    }
    // If it's not decompilable, merge the unlabelled blocks together
    auto back = result_out.blocks.begin();
    auto next = std::next(back);
    while (next != result_out.blocks.end()) {
        if (state.labels.count(next->start) == 0 && next->start == back->end + 1) {
            back->end = next->end;
            next = result_out.blocks.erase(next);
            continue;
        }
        back = next;
        next++;
    }
    return {result_out};
}
} // namespace VideoCommon::Shader
