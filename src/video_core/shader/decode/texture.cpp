// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Register;
using Tegra::Shader::TextureMiscMode;
using Tegra::Shader::TextureProcessMode;
using Tegra::Shader::TextureType;

static std::size_t GetCoordCount(TextureType texture_type) {
    switch (texture_type) {
    case TextureType::Texture1D:
        return 1;
    case TextureType::Texture2D:
        return 2;
    case TextureType::Texture3D:
    case TextureType::TextureCube:
        return 3;
    default:
        UNIMPLEMENTED_MSG("Unhandled texture type: {}", static_cast<u32>(texture_type));
        return 0;
    }
}

u32 ShaderIR::DecodeTexture(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::TEX: {
        UNIMPLEMENTED_IF_MSG(instr.tex.UsesMiscMode(TextureMiscMode::AOFFI),
                             "AOFFI is not implemented");

        if (instr.tex.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TEX.NODEP implementation is incomplete");
        }

        const TextureType texture_type{instr.tex.texture_type};
        const bool is_array = instr.tex.array != 0;
        const bool depth_compare = instr.tex.UsesMiscMode(TextureMiscMode::DC);
        const auto process_mode = instr.tex.GetTextureProcessMode();
        WriteTexInstructionFloat(
            bb, instr, GetTexCode(instr, texture_type, process_mode, depth_compare, is_array));
        break;
    }
    case OpCode::Id::TEXS: {
        const TextureType texture_type{instr.texs.GetTextureType()};
        const bool is_array{instr.texs.IsArrayTexture()};
        const bool depth_compare = instr.texs.UsesMiscMode(TextureMiscMode::DC);
        const auto process_mode = instr.texs.GetTextureProcessMode();

        if (instr.texs.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TEXS.NODEP implementation is incomplete");
        }

        const Node4 components =
            GetTexsCode(instr, texture_type, process_mode, depth_compare, is_array);

        if (instr.texs.fp32_flag) {
            WriteTexsInstructionFloat(bb, instr, components);
        } else {
            WriteTexsInstructionHalfFloat(bb, instr, components);
        }
        break;
    }
    case OpCode::Id::TLD4: {
        ASSERT(instr.tld4.array == 0);
        UNIMPLEMENTED_IF_MSG(instr.tld4.UsesMiscMode(TextureMiscMode::AOFFI),
                             "AOFFI is not implemented");
        UNIMPLEMENTED_IF_MSG(instr.tld4.UsesMiscMode(TextureMiscMode::NDV),
                             "NDV is not implemented");
        UNIMPLEMENTED_IF_MSG(instr.tld4.UsesMiscMode(TextureMiscMode::PTP),
                             "PTP is not implemented");

        if (instr.tld4.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TLD4.NODEP implementation is incomplete");
        }

        const auto texture_type = instr.tld4.texture_type.Value();
        const bool depth_compare = instr.tld4.UsesMiscMode(TextureMiscMode::DC);
        const bool is_array = instr.tld4.array != 0;
        WriteTexInstructionFloat(bb, instr,
                                 GetTld4Code(instr, texture_type, depth_compare, is_array));
        break;
    }
    case OpCode::Id::TLD4S: {
        UNIMPLEMENTED_IF_MSG(instr.tld4s.UsesMiscMode(TextureMiscMode::AOFFI),
                             "AOFFI is not implemented");
        if (instr.tld4s.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TLD4S.NODEP implementation is incomplete");
        }

        const bool depth_compare = instr.tld4s.UsesMiscMode(TextureMiscMode::DC);
        const Node op_a = GetRegister(instr.gpr8);
        const Node op_b = GetRegister(instr.gpr20);

        // TODO(Subv): Figure out how the sampler type is encoded in the TLD4S instruction.
        std::vector<Node> coords;
        if (depth_compare) {
            // Note: TLD4S coordinate encoding works just like TEXS's
            const Node op_y = GetRegister(instr.gpr8.Value() + 1);
            coords.push_back(op_a);
            coords.push_back(op_y);
            coords.push_back(op_b);
        } else {
            coords.push_back(op_a);
            coords.push_back(op_b);
        }
        const Node component = Immediate(static_cast<u32>(instr.tld4s.component));

        const auto& sampler =
            GetSampler(instr.sampler, TextureType::Texture2D, false, depth_compare);

        Node4 values;
        for (u32 element = 0; element < values.size(); ++element) {
            auto coords_copy = coords;
            MetaTexture meta{sampler, {}, {}, {}, {}, component, element};
            values[element] = Operation(OperationCode::TextureGather, meta, std::move(coords_copy));
        }

        WriteTexsInstructionFloat(bb, instr, values);
        break;
    }
    case OpCode::Id::TXQ: {
        if (instr.txq.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TXQ.NODEP implementation is incomplete");
        }

        // TODO: The new commits on the texture refactor, change the way samplers work.
        // Sadly, not all texture instructions specify the type of texture their sampler
        // uses. This must be fixed at a later instance.
        const auto& sampler =
            GetSampler(instr.sampler, Tegra::Shader::TextureType::Texture2D, false, false);

        u32 indexer = 0;
        switch (instr.txq.query_type) {
        case Tegra::Shader::TextureQueryType::Dimension: {
            for (u32 element = 0; element < 4; ++element) {
                if (!instr.txq.IsComponentEnabled(element)) {
                    continue;
                }
                MetaTexture meta{sampler, {}, {}, {}, {}, {}, element};
                const Node value =
                    Operation(OperationCode::TextureQueryDimensions, meta, GetRegister(instr.gpr8));
                SetTemporal(bb, indexer++, value);
            }
            for (u32 i = 0; i < indexer; ++i) {
                SetRegister(bb, instr.gpr0.Value() + i, GetTemporal(i));
            }
            break;
        }
        default:
            UNIMPLEMENTED_MSG("Unhandled texture query type: {}",
                              static_cast<u32>(instr.txq.query_type.Value()));
        }
        break;
    }
    case OpCode::Id::TMML: {
        UNIMPLEMENTED_IF_MSG(instr.tmml.UsesMiscMode(Tegra::Shader::TextureMiscMode::NDV),
                             "NDV is not implemented");

        if (instr.tmml.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TMML.NODEP implementation is incomplete");
        }

        auto texture_type = instr.tmml.texture_type.Value();
        const bool is_array = instr.tmml.array != 0;
        const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, false);

        std::vector<Node> coords;

        // TODO: Add coordinates for different samplers once other texture types are implemented.
        switch (texture_type) {
        case TextureType::Texture1D:
            coords.push_back(GetRegister(instr.gpr8));
            break;
        case TextureType::Texture2D:
            coords.push_back(GetRegister(instr.gpr8.Value() + 0));
            coords.push_back(GetRegister(instr.gpr8.Value() + 1));
            break;
        default:
            UNIMPLEMENTED_MSG("Unhandled texture type {}", static_cast<u32>(texture_type));

            // Fallback to interpreting as a 2D texture for now
            coords.push_back(GetRegister(instr.gpr8.Value() + 0));
            coords.push_back(GetRegister(instr.gpr8.Value() + 1));
            texture_type = TextureType::Texture2D;
        }

        for (u32 element = 0; element < 2; ++element) {
            auto params = coords;
            MetaTexture meta{sampler, {}, {}, {}, {}, {}, element};
            const Node value = Operation(OperationCode::TextureQueryLod, meta, std::move(params));
            SetTemporal(bb, element, value);
        }
        for (u32 element = 0; element < 2; ++element) {
            SetRegister(bb, instr.gpr0.Value() + element, GetTemporal(element));
        }

        break;
    }
    case OpCode::Id::TLDS: {
        const Tegra::Shader::TextureType texture_type{instr.tlds.GetTextureType()};
        const bool is_array{instr.tlds.IsArrayTexture()};

        UNIMPLEMENTED_IF_MSG(instr.tlds.UsesMiscMode(TextureMiscMode::AOFFI),
                             "AOFFI is not implemented");
        UNIMPLEMENTED_IF_MSG(instr.tlds.UsesMiscMode(TextureMiscMode::MZ), "MZ is not implemented");

        if (instr.tlds.UsesMiscMode(TextureMiscMode::NODEP)) {
            LOG_WARNING(HW_GPU, "TLDS.NODEP implementation is incomplete");
        }

        WriteTexsInstructionFloat(bb, instr, GetTldsCode(instr, texture_type, is_array));
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled memory instruction: {}", opcode->get().GetName());
    }

    return pc;
}

const Sampler& ShaderIR::GetSampler(const Tegra::Shader::Sampler& sampler, TextureType type,
                                    bool is_array, bool is_shadow) {
    const auto offset = static_cast<std::size_t>(sampler.index.Value());

    // If this sampler has already been used, return the existing mapping.
    const auto itr =
        std::find_if(used_samplers.begin(), used_samplers.end(),
                     [&](const Sampler& entry) { return entry.GetOffset() == offset; });
    if (itr != used_samplers.end()) {
        ASSERT(itr->GetType() == type && itr->IsArray() == is_array &&
               itr->IsShadow() == is_shadow);
        return *itr;
    }

    // Otherwise create a new mapping for this sampler
    const std::size_t next_index = used_samplers.size();
    const Sampler entry{offset, next_index, type, is_array, is_shadow};
    return *used_samplers.emplace(entry).first;
}

void ShaderIR::WriteTexInstructionFloat(NodeBlock& bb, Instruction instr, const Node4& components) {
    u32 dest_elem = 0;
    for (u32 elem = 0; elem < 4; ++elem) {
        if (!instr.tex.IsComponentEnabled(elem)) {
            // Skip disabled components
            continue;
        }
        SetTemporal(bb, dest_elem++, components[elem]);
    }
    // After writing values in temporals, move them to the real registers
    for (u32 i = 0; i < dest_elem; ++i) {
        SetRegister(bb, instr.gpr0.Value() + i, GetTemporal(i));
    }
}

void ShaderIR::WriteTexsInstructionFloat(NodeBlock& bb, Instruction instr,
                                         const Node4& components) {
    // TEXS has two destination registers and a swizzle. The first two elements in the swizzle
    // go into gpr0+0 and gpr0+1, and the rest goes into gpr28+0 and gpr28+1

    u32 dest_elem = 0;
    for (u32 component = 0; component < 4; ++component) {
        if (!instr.texs.IsComponentEnabled(component))
            continue;
        SetTemporal(bb, dest_elem++, components[component]);
    }

    for (u32 i = 0; i < dest_elem; ++i) {
        if (i < 2) {
            // Write the first two swizzle components to gpr0 and gpr0+1
            SetRegister(bb, instr.gpr0.Value() + i % 2, GetTemporal(i));
        } else {
            ASSERT(instr.texs.HasTwoDestinations());
            // Write the rest of the swizzle components to gpr28 and gpr28+1
            SetRegister(bb, instr.gpr28.Value() + i % 2, GetTemporal(i));
        }
    }
}

void ShaderIR::WriteTexsInstructionHalfFloat(NodeBlock& bb, Instruction instr,
                                             const Node4& components) {
    // TEXS.F16 destionation registers are packed in two registers in pairs (just like any half
    // float instruction).

    Node4 values;
    u32 dest_elem = 0;
    for (u32 component = 0; component < 4; ++component) {
        if (!instr.texs.IsComponentEnabled(component))
            continue;
        values[dest_elem++] = components[component];
    }
    if (dest_elem == 0)
        return;

    std::generate(values.begin() + dest_elem, values.end(), [&]() { return Immediate(0); });

    const Node first_value = Operation(OperationCode::HPack2, values[0], values[1]);
    if (dest_elem <= 2) {
        SetRegister(bb, instr.gpr0, first_value);
        return;
    }

    SetTemporal(bb, 0, first_value);
    SetTemporal(bb, 1, Operation(OperationCode::HPack2, values[2], values[3]));

    SetRegister(bb, instr.gpr0, GetTemporal(0));
    SetRegister(bb, instr.gpr28, GetTemporal(1));
}

Node4 ShaderIR::GetTextureCode(Instruction instr, TextureType texture_type,
                               TextureProcessMode process_mode, std::vector<Node> coords,
                               Node array, Node depth_compare, u32 bias_offset) {
    const bool is_array = array;
    const bool is_shadow = depth_compare;

    UNIMPLEMENTED_IF_MSG((texture_type == TextureType::Texture3D && (is_array || is_shadow)) ||
                             (texture_type == TextureType::TextureCube && is_array && is_shadow),
                         "This method is not supported.");

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, is_shadow);

    const bool lod_needed = process_mode == TextureProcessMode::LZ ||
                            process_mode == TextureProcessMode::LL ||
                            process_mode == TextureProcessMode::LLA;

    // LOD selection (either via bias or explicit textureLod) not supported in GL for
    // sampler2DArrayShadow and samplerCubeArrayShadow.
    const bool gl_lod_supported =
        !((texture_type == Tegra::Shader::TextureType::Texture2D && is_array && is_shadow) ||
          (texture_type == Tegra::Shader::TextureType::TextureCube && is_array && is_shadow));

    const OperationCode read_method =
        (lod_needed && gl_lod_supported) ? OperationCode::TextureLod : OperationCode::Texture;

    UNIMPLEMENTED_IF(process_mode != TextureProcessMode::None && !gl_lod_supported);

    Node bias = {};
    Node lod = {};
    if (process_mode != TextureProcessMode::None && gl_lod_supported) {
        switch (process_mode) {
        case TextureProcessMode::LZ:
            lod = Immediate(0.0f);
            break;
        case TextureProcessMode::LB:
            // If present, lod or bias are always stored in the register indexed by the gpr20
            // field with an offset depending on the usage of the other registers
            bias = GetRegister(instr.gpr20.Value() + bias_offset);
            break;
        case TextureProcessMode::LL:
            lod = GetRegister(instr.gpr20.Value() + bias_offset);
            break;
        default:
            UNIMPLEMENTED_MSG("Unimplemented process mode={}", static_cast<u32>(process_mode));
            break;
        }
    }

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto copy_coords = coords;
        MetaTexture meta{sampler, array, depth_compare, bias, lod, {}, element};
        values[element] = Operation(read_method, meta, std::move(copy_coords));
    }

    return values;
}

Node4 ShaderIR::GetTexCode(Instruction instr, TextureType texture_type,
                           TextureProcessMode process_mode, bool depth_compare, bool is_array) {
    const bool lod_bias_enabled =
        (process_mode != TextureProcessMode::None && process_mode != TextureProcessMode::LZ);

    const auto [coord_count, total_coord_count] = ValidateAndGetCoordinateElement(
        texture_type, depth_compare, is_array, lod_bias_enabled, 4, 5);
    // If enabled arrays index is always stored in the gpr8 field
    const u64 array_register = instr.gpr8.Value();
    // First coordinate index is the gpr8 or gpr8 + 1 when arrays are used
    const u64 coord_register = array_register + (is_array ? 1 : 0);

    std::vector<Node> coords;
    for (std::size_t i = 0; i < coord_count; ++i) {
        coords.push_back(GetRegister(coord_register + i));
    }
    // 1D.DC in OpenGL the 2nd component is ignored.
    if (depth_compare && !is_array && texture_type == TextureType::Texture1D) {
        coords.push_back(Immediate(0.0f));
    }

    const Node array = is_array ? GetRegister(array_register) : nullptr;

    Node dc{};
    if (depth_compare) {
        // Depth is always stored in the register signaled by gpr20 or in the next register if lod
        // or bias are used
        const u64 depth_register = instr.gpr20.Value() + (lod_bias_enabled ? 1 : 0);
        dc = GetRegister(depth_register);
    }

    return GetTextureCode(instr, texture_type, process_mode, coords, array, dc, 0);
}

Node4 ShaderIR::GetTexsCode(Instruction instr, TextureType texture_type,
                            TextureProcessMode process_mode, bool depth_compare, bool is_array) {
    const bool lod_bias_enabled =
        (process_mode != TextureProcessMode::None && process_mode != TextureProcessMode::LZ);

    const auto [coord_count, total_coord_count] = ValidateAndGetCoordinateElement(
        texture_type, depth_compare, is_array, lod_bias_enabled, 4, 4);
    // If enabled arrays index is always stored in the gpr8 field
    const u64 array_register = instr.gpr8.Value();
    // First coordinate index is stored in gpr8 field or (gpr8 + 1) when arrays are used
    const u64 coord_register = array_register + (is_array ? 1 : 0);
    const u64 last_coord_register =
        (is_array || !(lod_bias_enabled || depth_compare) || (coord_count > 2))
            ? static_cast<u64>(instr.gpr20.Value())
            : coord_register + 1;
    const u32 bias_offset = coord_count > 2 ? 1 : 0;

    std::vector<Node> coords;
    for (std::size_t i = 0; i < coord_count; ++i) {
        const bool last = (i == (coord_count - 1)) && (coord_count > 1);
        coords.push_back(GetRegister(last ? last_coord_register : coord_register + i));
    }

    const Node array = is_array ? GetRegister(array_register) : nullptr;

    Node dc{};
    if (depth_compare) {
        // Depth is always stored in the register signaled by gpr20 or in the next register if lod
        // or bias are used
        const u64 depth_register = instr.gpr20.Value() + (lod_bias_enabled ? 1 : 0);
        dc = GetRegister(depth_register);
    }

    return GetTextureCode(instr, texture_type, process_mode, coords, array, dc, bias_offset);
}

Node4 ShaderIR::GetTld4Code(Instruction instr, TextureType texture_type, bool depth_compare,
                            bool is_array) {
    const std::size_t coord_count = GetCoordCount(texture_type);
    const std::size_t total_coord_count = coord_count + (is_array ? 1 : 0);
    const std::size_t total_reg_count = total_coord_count + (depth_compare ? 1 : 0);

    // If enabled arrays index is always stored in the gpr8 field
    const u64 array_register = instr.gpr8.Value();
    // First coordinate index is the gpr8 or gpr8 + 1 when arrays are used
    const u64 coord_register = array_register + (is_array ? 1 : 0);

    std::vector<Node> coords;
    for (size_t i = 0; i < coord_count; ++i)
        coords.push_back(GetRegister(coord_register + i));

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, depth_compare);

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto coords_copy = coords;
        MetaTexture meta{sampler, GetRegister(array_register), {}, {}, {}, {}, element};
        values[element] = Operation(OperationCode::TextureGather, meta, std::move(coords_copy));
    }

    return values;
}

Node4 ShaderIR::GetTldsCode(Instruction instr, TextureType texture_type, bool is_array) {
    const std::size_t type_coord_count = GetCoordCount(texture_type);
    const bool lod_enabled = instr.tlds.GetTextureProcessMode() == TextureProcessMode::LL;

    // If enabled arrays index is always stored in the gpr8 field
    const u64 array_register = instr.gpr8.Value();
    // if is array gpr20 is used
    const u64 coord_register = is_array ? instr.gpr20.Value() : instr.gpr8.Value();

    const u64 last_coord_register =
        ((type_coord_count > 2) || (type_coord_count == 2 && !lod_enabled)) && !is_array
            ? static_cast<u64>(instr.gpr20.Value())
            : coord_register + 1;

    std::vector<Node> coords;
    for (std::size_t i = 0; i < type_coord_count; ++i) {
        const bool last = (i == (type_coord_count - 1)) && (type_coord_count > 1);
        coords.push_back(GetRegister(last ? last_coord_register : coord_register + i));
    }

    const Node array = is_array ? GetRegister(array_register) : nullptr;
    // When lod is used always is in gpr20
    const Node lod = lod_enabled ? GetRegister(instr.gpr20) : Immediate(0);

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, false);

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto coords_copy = coords;
        MetaTexture meta{sampler, array, {}, {}, lod, {}, element};
        values[element] = Operation(OperationCode::TexelFetch, meta, std::move(coords_copy));
    }
    return values;
}

std::tuple<std::size_t, std::size_t> ShaderIR::ValidateAndGetCoordinateElement(
    TextureType texture_type, bool depth_compare, bool is_array, bool lod_bias_enabled,
    std::size_t max_coords, std::size_t max_inputs) {
    const std::size_t coord_count = GetCoordCount(texture_type);

    std::size_t total_coord_count = coord_count + (is_array ? 1 : 0) + (depth_compare ? 1 : 0);
    const std::size_t total_reg_count = total_coord_count + (lod_bias_enabled ? 1 : 0);
    if (total_coord_count > max_coords || total_reg_count > max_inputs) {
        UNIMPLEMENTED_MSG("Unsupported Texture operation");
        total_coord_count = std::min(total_coord_count, max_coords);
    }
    // 1D.DC OpenGL is using a vec3 but 2nd component is ignored later.
    total_coord_count +=
        (depth_compare && !is_array && texture_type == TextureType::Texture1D) ? 1 : 0;

    return {coord_count, total_coord_count};
}

} // namespace VideoCommon::Shader