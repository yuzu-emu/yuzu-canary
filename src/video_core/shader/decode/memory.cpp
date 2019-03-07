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

u32 ShaderIR::DecodeMemory(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::LD_A: {
        // Note: Shouldn't this be interp mode flat? As in no interpolation made.
        UNIMPLEMENTED_IF_MSG(instr.gpr8.Value() != Register::ZeroIndex,
                             "Indirect attribute loads are not supported");
        UNIMPLEMENTED_IF_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) != 0,
                             "Unaligned attribute loads are not supported");

        Tegra::Shader::IpaMode input_mode{Tegra::Shader::IpaInterpMode::Pass,
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
            GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.GetOffset() + 0, index);

        switch (instr.ld_c.type.Value()) {
        case Tegra::Shader::UniformType::Single:
            SetRegister(bb, instr.gpr0, op_a);
            break;

        case Tegra::Shader::UniformType::Double: {
            const Node op_b =
                GetConstBufferIndirect(instr.cbuf36.index, instr.cbuf36.GetOffset() + 4, index);

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
                             static_cast<u32>(instr.ld_l.unknown.Value()));

        const auto GetLmem = [&](s32 offset) {
            ASSERT(offset % 4 == 0);
            const Node immediate_offset = Immediate(static_cast<s32>(instr.smem_imm) + offset);
            const Node address = Operation(OperationCode::IAdd, NO_PRECISE, GetRegister(instr.gpr8),
                                           immediate_offset);
            return GetLocalMemory(address);
        };

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bits32:
        case Tegra::Shader::StoreType::Bits64:
        case Tegra::Shader::StoreType::Bits128: {
            const u32 count = [&]() {
                switch (instr.ldst_sl.type.Value()) {
                case Tegra::Shader::StoreType::Bits32:
                    return 1;
                case Tegra::Shader::StoreType::Bits64:
                    return 2;
                case Tegra::Shader::StoreType::Bits128:
                    return 4;
                default:
                    UNREACHABLE();
                    return 0;
                }
            }();
            for (u32 i = 0; i < count; ++i)
                SetTemporal(bb, i, GetLmem(i * 4));
            for (u32 i = 0; i < count; ++i)
                SetRegister(bb, instr.gpr0.Value() + i, GetTemporal(i));
            break;
        }
        default:
            UNIMPLEMENTED_MSG("LD_L Unhandled type: {}",
                              static_cast<u32>(instr.ldst_sl.type.Value()));
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
        const Node base_address =
            TrackCbuf(addr_register, global_code, static_cast<s64>(global_code.size()));
        const auto cbuf = std::get_if<CbufNode>(base_address);
        ASSERT(cbuf != nullptr);
        const auto cbuf_offset_imm = std::get_if<ImmediateNode>(cbuf->GetOffset());
        ASSERT(cbuf_offset_imm != nullptr);
        const auto cbuf_offset = cbuf_offset_imm->GetValue();

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

        const auto GetLmemAddr = [&](s32 offset) {
            ASSERT(offset % 4 == 0);
            const Node immediate = Immediate(static_cast<s32>(instr.smem_imm) + offset);
            return Operation(OperationCode::IAdd, NO_PRECISE, GetRegister(instr.gpr8), immediate);
        };

        switch (instr.ldst_sl.type.Value()) {
        case Tegra::Shader::StoreType::Bits128:
            SetLocalMemory(bb, GetLmemAddr(12), GetRegister(instr.gpr0.Value() + 3));
            SetLocalMemory(bb, GetLmemAddr(8), GetRegister(instr.gpr0.Value() + 2));
        case Tegra::Shader::StoreType::Bits64:
            SetLocalMemory(bb, GetLmemAddr(4), GetRegister(instr.gpr0.Value() + 1));
        case Tegra::Shader::StoreType::Bits32:
            SetLocalMemory(bb, GetLmemAddr(0), GetRegister(instr.gpr0));
            break;
        default:
            UNIMPLEMENTED_MSG("ST_L Unhandled type: {}",
                              static_cast<u32>(instr.ldst_sl.type.Value()));
        }
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled memory instruction: {}", opcode->get().GetName());
    }

    return pc;
}

} // namespace VideoCommon::Shader
