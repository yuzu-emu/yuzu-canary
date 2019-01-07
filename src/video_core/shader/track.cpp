// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>
#include <variant>

#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

namespace {
std::pair<Node, s64> FindOperation(const BasicBlock& code, s64 cursor,
                                   OperationCode operation_code) {
    for (; cursor >= 0; --cursor) {
        const Node node = code[cursor];
        if (const auto operation = std::get_if<OperationNode>(node)) {
            if (operation->GetCode() == operation_code)
                return {node, cursor};
        }
    }
    return {};
}
} // namespace

Node ShaderIR::TrackCbuf(Node tracked, const BasicBlock& code, s64 cursor) {
    if (const auto cbuf = std::get_if<CbufNode>(tracked)) {
        // Cbuf found, but it has to be immediate
        return std::holds_alternative<ImmediateNode>(*cbuf->GetOffset()) ? tracked : nullptr;
    }
    if (const auto gpr = std::get_if<GprNode>(tracked)) {
        if (gpr->GetIndex() == Tegra::Shader::Register::ZeroIndex) {
            return nullptr;
        }
        // Reduce the cursor in one to avoid infinite loops when the instruction sets the same
        // register that it uses as operand
        const auto [source, new_cursor] = TrackRegister(gpr, code, cursor - 1);
        if (!source) {
            return nullptr;
        }
        return TrackCbuf(source, code, new_cursor);
    }
    if (const auto operation = std::get_if<OperationNode>(tracked)) {
        for (std::size_t i = 0; i < operation->GetOperandsCount(); ++i) {
            if (const auto found = TrackCbuf((*operation)[i], code, cursor)) {
                // Cbuf found in operand
                return found;
            }
        }
        return nullptr;
    }
    return nullptr;
}

std::pair<Node, s64> ShaderIR::TrackRegister(const GprNode* tracked, const BasicBlock& code,
                                             s64 cursor) {
    for (; cursor >= 0; --cursor) {
        const auto [found_node, new_cursor] = FindOperation(code, cursor, OperationCode::Assign);
        if (!found_node) {
            return {};
        }
        const auto& operation = std::get<OperationNode>(*found_node);
        const auto& target = operation[0];
        if (const auto gpr_target = std::get_if<GprNode>(target)) {
            if (gpr_target->GetIndex() == tracked->GetIndex()) {
                return {operation[1], new_cursor};
            }
        }
    }
    return {};
}

} // namespace VideoCommon::Shader