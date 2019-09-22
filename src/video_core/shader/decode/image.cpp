// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/node_helper.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

namespace {
std::size_t GetImageTypeNumCoordinates(Tegra::Shader::ImageType image_type) {
    switch (image_type) {
    case Tegra::Shader::ImageType::Texture1D:
    case Tegra::Shader::ImageType::TextureBuffer:
        return 1;
    case Tegra::Shader::ImageType::Texture1DArray:
    case Tegra::Shader::ImageType::Texture2D:
        return 2;
    case Tegra::Shader::ImageType::Texture2DArray:
    case Tegra::Shader::ImageType::Texture3D:
        return 3;
    }
    UNREACHABLE();
    return 1;
}
} // Anonymous namespace

u32 ShaderIR::DecodeImage(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    const auto GetCoordinates = [this, instr](Tegra::Shader::ImageType image_type) {
        std::vector<Node> coords;
        const std::size_t num_coords{GetImageTypeNumCoordinates(image_type)};
        coords.reserve(num_coords);
        for (std::size_t i = 0; i < num_coords; ++i) {
            coords.push_back(GetRegister(instr.gpr8.Value() + i));
        }
        return coords;
    };

    switch (opcode->get().GetId()) {
    case OpCode::Id::SULD: {
        UNIMPLEMENTED_IF(instr.suldst.mode != Tegra::Shader::SurfaceDataMode::P);
        UNIMPLEMENTED_IF(instr.suldst.out_of_bounds_store !=
                         Tegra::Shader::OutOfBoundsStore::Ignore);

        const auto type{instr.suldst.image_type};
        auto& image{instr.suldst.is_immediate ? GetImage(instr.image, type)
                                              : GetBindlessImage(instr.gpr39, type)};
        image.MarkRead();

        u32 indexer = 0;
        for (u32 element = 0; element < 4; ++element) {
            if (!instr.suldst.IsComponentEnabled(element)) {
                continue;
            }
            MetaImage meta{image, {}, element};
            Node value = Operation(OperationCode::ImageLoad, meta, GetCoordinates(type));
            SetTemporary(bb, indexer++, std::move(value));
        }
        for (u32 i = 0; i < indexer; ++i) {
            SetRegister(bb, instr.gpr0.Value() + i, GetTemporary(i));
        }
        break;
    }
    case OpCode::Id::SUST: {
        UNIMPLEMENTED_IF(instr.suldst.mode != Tegra::Shader::SurfaceDataMode::P);
        UNIMPLEMENTED_IF(instr.suldst.out_of_bounds_store !=
                         Tegra::Shader::OutOfBoundsStore::Ignore);
        UNIMPLEMENTED_IF(instr.suldst.component_mask_selector != 0xf); // Ensure we have RGBA

        std::vector<Node> values;
        constexpr std::size_t hardcoded_size{4};
        for (std::size_t i = 0; i < hardcoded_size; ++i) {
            values.push_back(GetRegister(instr.gpr0.Value() + i));
        }

        const auto type{instr.suldst.image_type};
        auto& image{instr.suldst.is_immediate ? GetImage(instr.image, type)
                                              : GetBindlessImage(instr.gpr39, type)};
        image.MarkWrite();

        MetaImage meta{image, std::move(values)};
        bb.push_back(Operation(OperationCode::ImageStore, meta, GetCoordinates(type)));
        break;
    }
    case OpCode::Id::SUATOM: {
        UNIMPLEMENTED_IF(instr.suatom_d.is_ba != 0);

        const OperationCode operation_code = [instr] {
            switch (instr.suatom_d.operation_type) {
            case Tegra::Shader::ImageAtomicOperationType::S32:
            case Tegra::Shader::ImageAtomicOperationType::U32:
                switch (instr.suatom_d.operation) {
                case Tegra::Shader::ImageAtomicOperation::Add:
                    return OperationCode::AtomicImageAdd;
                case Tegra::Shader::ImageAtomicOperation::And:
                    return OperationCode::AtomicImageAnd;
                case Tegra::Shader::ImageAtomicOperation::Or:
                    return OperationCode::AtomicImageOr;
                case Tegra::Shader::ImageAtomicOperation::Xor:
                    return OperationCode::AtomicImageXor;
                case Tegra::Shader::ImageAtomicOperation::Exch:
                    return OperationCode::AtomicImageExchange;
                }
            default:
                break;
            }
            UNIMPLEMENTED_MSG("Unimplemented operation={} type={}",
                              static_cast<u64>(instr.suatom_d.operation.Value()),
                              static_cast<u64>(instr.suatom_d.operation_type.Value()));
            return OperationCode::AtomicImageAdd;
        }();

        Node value = GetRegister(instr.gpr0);

        const auto type = instr.suatom_d.image_type;
        auto& image = GetImage(instr.image, type);
        image.MarkAtomic();

        MetaImage meta{image, {std::move(value)}};
        SetRegister(bb, instr.gpr0, Operation(operation_code, meta, GetCoordinates(type)));
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled image instruction: {}", opcode->get().GetName());
    }

    return pc;
}

Image& ShaderIR::GetImage(Tegra::Shader::Image image, Tegra::Shader::ImageType type) {
    const auto offset{static_cast<std::size_t>(image.index.Value())};
    if (const auto image = TryUseExistingImage(offset, type)) {
        return *image;
    }

    const std::size_t next_index{used_images.size()};
    return used_images.emplace(offset, Image{offset, next_index, type}).first->second;
}

Image& ShaderIR::GetBindlessImage(Tegra::Shader::Register reg, Tegra::Shader::ImageType type) {
    const Node image_register{GetRegister(reg)};
    const auto [base_image, cbuf_index, cbuf_offset]{
        TrackCbuf(image_register, global_code, static_cast<s64>(global_code.size()))};
    const auto cbuf_key{(static_cast<u64>(cbuf_index) << 32) | static_cast<u64>(cbuf_offset)};

    if (const auto image = TryUseExistingImage(cbuf_key, type)) {
        return *image;
    }

    const std::size_t next_index{used_images.size()};
    return used_images.emplace(cbuf_key, Image{cbuf_index, cbuf_offset, next_index, type})
        .first->second;
}

Image* ShaderIR::TryUseExistingImage(u64 offset, Tegra::Shader::ImageType type) {
    auto it = used_images.find(offset);
    if (it == used_images.end()) {
        return nullptr;
    }
    auto& image = it->second;
    ASSERT(image.GetType() == type);

    return &image;
}

} // namespace VideoCommon::Shader
