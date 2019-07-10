// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <list>
#include <optional>
#include <unordered_set>

#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::ConditionCode;
using Tegra::Shader::Pred;

constexpr s32 exit_branch = -1;

struct Condition {
    Pred predicate{Pred::UnusedIndex};
    ConditionCode cc{ConditionCode::T};

    bool IsUnconditional() const {
        return predicate == Pred::UnusedIndex && cc == ConditionCode::T;
    }
    bool operator==(const Condition& other) const {
        return std::tie(predicate, cc) == std::tie(other.predicate, other.cc);
    }
};

struct ShaderBlock {
    u32 start{};
    u32 end{};
    bool ignore_branch{};
    struct Branch {
        Condition cond{};
        bool kills{};
        s32 address{};
        bool operator==(const Branch& b) const {
            return std::tie(cond, kills, address) == std::tie(b.cond, b.kills, b.address);
        }
    } branch{};
    bool operator==(const ShaderBlock& sb) const {
        return std::tie(start, end, ignore_branch, branch) ==
               std::tie(sb.start, sb.end, sb.ignore_branch, sb.branch);
    }
};

struct ShaderCharacteristics {
    std::list<ShaderBlock> blocks{};
    bool decompilable{};
    u32 start{};
    u32 end{};
    std::unordered_set<u32> labels{};
};

std::optional<ShaderCharacteristics> ScanFlow(const ProgramCode& program_code, u32 program_size,
                                              u32 start_address);

} // namespace VideoCommon::Shader
