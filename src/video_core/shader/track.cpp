// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <utility>
#include <variant>

#include "common/common_types.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

namespace {
std::pair<Node, s64> FindOperation(const NodeBlock& code, s64 cursor,
                                   OperationCode operation_code) {
    for (; cursor >= 0; --cursor) {
        const Node node = code.at(cursor);
        if (const auto operation = std::get_if<OperationNode>(node)) {
            if (operation->GetCode() == operation_code)
                return {node, cursor};
        }
        if (const auto conditional = std::get_if<ConditionalNode>(node)) {
            const auto& conditional_code = conditional->GetCode();
            const auto [found, internal_cursor] = FindOperation(
                conditional_code, static_cast<s64>(conditional_code.size() - 1), operation_code);
            if (found)
                return {found, cursor};
        }
    }
    return {};
}
} // namespace

Node ShaderIR::TrackCbuf(Node tracked, const NodeBlock& code, s64 cursor) {
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
    if (const auto conditional = std::get_if<ConditionalNode>(tracked)) {
        const auto& conditional_code = conditional->GetCode();
        return TrackCbuf(tracked, conditional_code, static_cast<s64>(conditional_code.size()));
    }
    return nullptr;
}

std::optional<u32> ShaderIR::TrackImmediate(Node tracked, const NodeBlock& code, s64 cursor) {
    // Reduce the cursor in one to avoid infinite loops when the instruction sets the same register
    // that it uses as operand
    const auto [found, found_cursor] =
        TrackRegister(&std::get<GprNode>(*tracked), code, cursor - 1);
    if (!found) {
        return {};
    }
    if (const auto immediate = std::get_if<ImmediateNode>(found)) {
        return immediate->GetValue();
    }
    return {};
}

std::pair<Node, s64> ShaderIR::TrackRegister(const GprNode* tracked, const NodeBlock& code,
                                             s64 cursor) {
    for (; cursor >= 0; --cursor) {
        const auto [found_node, new_cursor] = FindOperation(code, cursor, OperationCode::Assign);
        if (!found_node) {
            return {};
        }
        const auto operation = std::get_if<OperationNode>(found_node);
        ASSERT(operation);

        const auto& target = (*operation)[0];
        if (const auto gpr_target = std::get_if<GprNode>(target)) {
            if (gpr_target->GetIndex() == tracked->GetIndex()) {
                return {(*operation)[1], new_cursor};
            }
        }
    }
    return {};
}

} // namespace VideoCommon::Shader
