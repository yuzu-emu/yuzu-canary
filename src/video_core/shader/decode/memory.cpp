// Copyright 2018 yuzu Emulator Project
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

using Tegra::Shader::Attribute;
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

u32 ShaderIR::DecodeMemory(BasicBlock& bb, const BasicBlock& code, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::LD_A: {
        // Note: Shouldn't this be interp mode flat? As in no interpolation made.
        UNIMPLEMENTED_IF_MSG(instr.gpr8.Value() != Register::ZeroIndex,
                             "Indirect attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) != 0,
                             "Unaligned attribute loads are not supported");

        Tegra::Shader::IpaMode input_mode{Tegra::Shader::IpaInterpMode::Perspective,
                                          Tegra::Shader::IpaSampleMode::Default};

        u64 next_element = instr.attribute.fmt20.element;
        auto next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

        const auto LoadNextElement = [&](u32 reg_offset) {
            const Node buffer = GetRegister(instr.gpr39);
            const Node attribute = GetInputAttribute(static_cast<Attribute::Index>(next_index),
                                                     next_element, input_mode, buffer);

            SetRegister(bb, instr.gpr0.Value() + reg_offset, attribute);

            // Load the next attribute element into the following register. If the element
            // to load goes beyond the vec4 size, load the first element of the next
            // attribute.
            next_element = (next_element + 1) % 4;
            next_index = next_index + (next_element == 0 ? 1 : 0);
        };

        const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
        for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
            LoadNextElement(reg_offset);
        }
        break;
    }
    case OpCode::Id::LD_C: {
        UNIMPLEMENTED_IF(instr.ld_c.unknown != 0);

        Node index = GetRegister(instr.gpr8);

        const Node op_a =
            GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.offset + 0, index);

        switch (instr.ld_c.type.Value()) {
        case Tegra::Shader::UniformType::Single:
            SetRegister(bb, instr.gpr0, op_a);
            break;

        case Tegra::Shader::UniformType::Double: {
            const Node op_b =
                GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.offset + 4, index);

            SetTemporal(bb, 0, op_a);
            SetTemporal(bb, 1, op_b);
            SetRegister(bb, instr.gpr0, GetTemporal(0));
            SetRegister(bb, instr.gpr0.Value() + 1, GetTemporal(1));
            break;
        }
        default:
            UNIMPLEMENTED_MSG("Unhandled type: {}", static_cast<unsigned>(instr.ld_c.type.Value()));
        }
        break;
    }
    case OpCode::Id::LD_L: {
        UNIMPLEMENTED_IF_MSG(instr.ld_l.unknown == 1, "LD_L Unhandled mode: {}",
                             static_cast<unsigned>(instr.ld_l.unknown.Value()));

        const Node index = Operation(OperationCode::IAdd, GetRegister(instr.gpr8),
                                     Immediate(static_cast<s32>(instr.smem_imm)));
        const Node lmem = GetLocalMemory(index);

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bytes32:
            SetRegister(bb, instr.gpr0, lmem);
            break;
        default:
            UNIMPLEMENTED_MSG("LD_L Unhandled type: {}",
                              static_cast<unsigned>(instr.ldst_sl.type.Value()));
        }
        break;
    }
    case OpCode::Id::LDG: {
        const u32 count = [&]() {
            switch (instr.ldg.type) {
            case Tegra::Shader::UniformType::Single:
                return 1;
            case Tegra::Shader::UniformType::Double:
                return 2;
            case Tegra::Shader::UniformType::Quad:
            case Tegra::Shader::UniformType::UnsignedQuad:
                return 4;
            default:
                UNIMPLEMENTED_MSG("Unimplemented LDG size!");
                return 1;
            }
        }();

        const Node addr_register = GetRegister(instr.gpr8);
        const Node base_address = TrackCbuf(addr_register, code, static_cast<s64>(code.size()));
        const auto cbuf = std::get_if<CbufNode>(base_address);
        ASSERT(cbuf != nullptr);
        const auto cbuf_offset_imm = std::get_if<ImmediateNode>(cbuf->GetOffset());
        ASSERT(cbuf_offset_imm != nullptr);
        const auto cbuf_offset = cbuf_offset_imm->GetValue() * 4;

        bb.push_back(Comment(
            fmt::format("Base address is c[0x{:x}][0x{:x}]", cbuf->GetIndex(), cbuf_offset)));

        const GlobalMemoryBase descriptor{cbuf->GetIndex(), cbuf_offset};
        used_global_memory_bases.insert(descriptor);

        const Node immediate_offset =
            Immediate(static_cast<u32>(instr.ldg.immediate_offset.Value()));
        const Node base_real_address =
            Operation(OperationCode::UAdd, NO_PRECISE, immediate_offset, addr_register);

        for (u32 i = 0; i < count; ++i) {
            const Node it_offset = Immediate(i * 4);
            const Node real_address =
                Operation(OperationCode::UAdd, NO_PRECISE, base_real_address, it_offset);
            const Node gmem = StoreNode(GmemNode(real_address, base_address, descriptor));

            SetTemporal(bb, i, gmem);
        }
        for (u32 i = 0; i < count; ++i) {
            SetRegister(bb, instr.gpr0.Value() + i, GetTemporal(i));
        }
        break;
    }
    case OpCode::Id::ST_A: {
        UNIMPLEMENTED_IF_MSG(instr.gpr8.Value() != Register::ZeroIndex,
                             "Indirect attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) != 0,
                             "Unaligned attribute loads are not supported");

        u64 next_element = instr.attribute.fmt20.element;
        auto next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

        const auto StoreNextElement = [&](u32 reg_offset) {
            const auto dest = GetOutputAttribute(static_cast<Attribute::Index>(next_index),
                                                 next_element, GetRegister(instr.gpr39));
            const auto src = GetRegister(instr.gpr0.Value() + reg_offset);

            bb.push_back(Operation(OperationCode::Assign, dest, src));

            // Load the next attribute element into the following register. If the element
            // to load goes beyond the vec4 size, load the first element of the next
            // attribute.
            next_element = (next_element + 1) % 4;
            next_index = next_index + (next_element == 0 ? 1 : 0);
        };

        const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
        for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
            StoreNextElement(reg_offset);
        }

        break;
    }
    case OpCode::Id::ST_L: {
        UNIMPLEMENTED_IF_MSG(instr.st_l.unknown == 0, "ST_L Unhandled mode: {}",
                             static_cast<u32>(instr.st_l.unknown.Value()));

        const Node index = Operation(OperationCode::IAdd, NO_PRECISE, GetRegister(instr.gpr8),
                                     Immediate(static_cast<s32>(instr.smem_imm)));

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bytes32:
            SetLocalMemory(bb, index, GetRegister(instr.gpr0));
            break;
        default:
            UNIMPLEMENTED_MSG("ST_L Unhandled type: {}",
                              static_cast<u32>(instr.ldst_sl.type.Value()));
        }
        break;
    }
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

        std::vector<Node> coords;

        // TODO(Subv): Figure out how the sampler type is encoded in the TLD4S instruction.
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
        const auto num_coords = static_cast<u32>(coords.size());
        coords.push_back(Immediate(static_cast<u32>(instr.tld4s.component)));

        const auto& sampler =
            GetSampler(instr.sampler, TextureType::Texture2D, false, depth_compare);

        Node4 values;
        for (u32 element = 0; element < values.size(); ++element) {
            auto params = coords;
            MetaTexture meta{sampler, element, num_coords};
            values[element] =
                Operation(OperationCode::F4TextureGather, std::move(meta), std::move(params));
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

        switch (instr.txq.query_type) {
        case Tegra::Shader::TextureQueryType::Dimension: {
            for (u32 element = 0; element < 4; ++element) {
                MetaTexture meta{sampler, element};
                const Node value = Operation(OperationCode::F4TextureQueryDimensions,
                                             std::move(meta), GetRegister(instr.gpr8));
                SetTemporal(bb, element, value);
            }
            for (u32 i = 0; i < 4; ++i) {
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
            MetaTexture meta_texture{sampler, element, static_cast<u32>(coords.size())};
            const Node value =
                Operation(OperationCode::F4TextureQueryLod, meta_texture, std::move(params));
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
            LOG_WARNING(HW_GPU, "TMML.NODEP implementation is incomplete");
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

void ShaderIR::WriteTexInstructionFloat(BasicBlock& bb, Instruction instr,
                                        const Node4& components) {
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

void ShaderIR::WriteTexsInstructionFloat(BasicBlock& bb, Instruction instr,
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

void ShaderIR::WriteTexsInstructionHalfFloat(BasicBlock& bb, Instruction instr,
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
                               TextureProcessMode process_mode, bool depth_compare, bool is_array,
                               std::size_t array_offset, std::size_t bias_offset,
                               std::vector<Node>&& coords) {
    UNIMPLEMENTED_IF_MSG(
        (texture_type == TextureType::Texture3D && (is_array || depth_compare)) ||
            (texture_type == TextureType::TextureCube && is_array && depth_compare),
        "This method is not supported.");

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, depth_compare);

    const bool lod_needed = process_mode == TextureProcessMode::LZ ||
                            process_mode == TextureProcessMode::LL ||
                            process_mode == TextureProcessMode::LLA;

    // LOD selection (either via bias or explicit textureLod) not supported in GL for
    // sampler2DArrayShadow and samplerCubeArrayShadow.
    const bool gl_lod_supported =
        !((texture_type == Tegra::Shader::TextureType::Texture2D && is_array && depth_compare) ||
          (texture_type == Tegra::Shader::TextureType::TextureCube && is_array && depth_compare));

    const OperationCode read_method =
        lod_needed && gl_lod_supported ? OperationCode::F4TextureLod : OperationCode::F4Texture;

    UNIMPLEMENTED_IF(process_mode != TextureProcessMode::None && !gl_lod_supported);

    std::optional<u32> array_offset_value;
    if (is_array)
        array_offset_value = static_cast<u32>(array_offset);

    const auto coords_count = static_cast<u32>(coords.size());

    if (process_mode != TextureProcessMode::None && gl_lod_supported) {
        if (process_mode == TextureProcessMode::LZ) {
            coords.push_back(Immediate(0.0f));
        } else {
            // If present, lod or bias are always stored in the register indexed by the gpr20
            // field with an offset depending on the usage of the other registers
            coords.push_back(GetRegister(instr.gpr20.Value() + bias_offset));
        }
    }

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto params = coords;
        MetaTexture meta{sampler, element, coords_count, array_offset_value};
        values[element] = Operation(read_method, std::move(meta), std::move(params));
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
    // 1D.DC in opengl the 2nd component is ignored.
    if (depth_compare && !is_array && texture_type == TextureType::Texture1D) {
        coords.push_back(Immediate(0.0f));
    }
    std::size_t array_offset{};
    if (is_array) {
        array_offset = coords.size();
        coords.push_back(GetRegister(array_register));
    }
    if (depth_compare) {
        // Depth is always stored in the register signaled by gpr20
        // or in the next register if lod or bias are used
        const u64 depth_register = instr.gpr20.Value() + (lod_bias_enabled ? 1 : 0);
        coords.push_back(GetRegister(depth_register));
    }
    // Fill ignored coordinates
    while (coords.size() < total_coord_count) {
        coords.push_back(Immediate(0));
    }

    return GetTextureCode(instr, texture_type, process_mode, depth_compare, is_array, array_offset,
                          0, std::move(coords));
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

    std::vector<Node> coords;
    for (std::size_t i = 0; i < coord_count; ++i) {
        const bool last = (i == (coord_count - 1)) && (coord_count > 1);
        coords.push_back(GetRegister(last ? last_coord_register : coord_register + i));
    }

    std::size_t array_offset{};
    if (is_array) {
        array_offset = coords.size();
        coords.push_back(GetRegister(array_register));
    }
    if (depth_compare) {
        // Depth is always stored in the register signaled by gpr20
        // or in the next register if lod or bias are used
        const u64 depth_register = instr.gpr20.Value() + (lod_bias_enabled ? 1 : 0);
        coords.push_back(GetRegister(depth_register));
    }
    // Fill ignored coordinates
    while (coords.size() < total_coord_count) {
        coords.push_back(Immediate(0));
    }

    return GetTextureCode(instr, texture_type, process_mode, depth_compare, is_array, array_offset,
                          (coord_count > 2 ? 1 : 0), std::move(coords));
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

    for (size_t i = 0; i < coord_count; ++i) {
        coords.push_back(GetRegister(coord_register + i));
    }
    std::optional<u32> array_offset;
    if (is_array) {
        array_offset = static_cast<u32>(coords.size());
        coords.push_back(GetRegister(array_register));
    }

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, depth_compare);

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto params = coords;
        MetaTexture meta{sampler, element, static_cast<u32>(coords.size()), array_offset};
        values[element] =
            Operation(OperationCode::F4TextureGather, std::move(meta), std::move(params));
    }

    return values;
}

Node4 ShaderIR::GetTldsCode(Instruction instr, TextureType texture_type, bool is_array) {
    const std::size_t type_coord_count = GetCoordCount(texture_type);
    const std::size_t total_coord_count = type_coord_count + (is_array ? 1 : 0);
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
    std::optional<u32> array_offset;
    if (is_array) {
        array_offset = static_cast<u32>(coords.size());
        coords.push_back(GetRegister(array_register));
    }
    const auto coords_count = static_cast<u32>(coords.size());

    if (lod_enabled) {
        // When lod is used always is in grp20
        coords.push_back(GetRegister(instr.gpr20));
    } else {
        coords.push_back(Immediate(0));
    }

    const auto& sampler = GetSampler(instr.sampler, texture_type, is_array, false);

    Node4 values;
    for (u32 element = 0; element < values.size(); ++element) {
        auto params = coords;
        MetaTexture meta{sampler, element, coords_count, array_offset};
        values[element] =
            Operation(OperationCode::F4TexelFetch, std::move(meta), std::move(params));
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