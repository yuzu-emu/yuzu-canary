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

    switch (opcode->get().GetId()) {
    case OpCode::Id::SUST: {
        UNIMPLEMENTED_IF(instr.sust.mode != Tegra::Shader::SurfaceDataMode::P);
        UNIMPLEMENTED_IF(instr.sust.out_of_bounds_store != Tegra::Shader::OutOfBoundsStore::Ignore);
        UNIMPLEMENTED_IF(instr.sust.component_mask_selector != 0xf); // Ensure we have an RGBA store

        std::vector<Node> values;
        constexpr std::size_t hardcoded_size{4};
        for (std::size_t i = 0; i < hardcoded_size; ++i) {
            values.push_back(GetRegister(instr.gpr0.Value() + i));
        }

        std::vector<Node> coords;
        const std::size_t num_coords{GetImageTypeNumCoordinates(instr.sust.image_type)};
        for (std::size_t i = 0; i < num_coords; ++i) {
            coords.push_back(GetRegister(instr.gpr8.Value() + i));
        }

        const auto type{instr.sust.image_type};
        auto& image{instr.sust.is_immediate ? GetImage(instr.image, type)
                                            : GetBindlessImage(instr.gpr39, type)};
        image.MarkWrite();

        MetaImage meta{image, values};
        bb.push_back(Operation(OperationCode::ImageStore, meta, std::move(coords)));
        break;
    }
    case OpCode::Id::SUATOM: {
        UNIMPLEMENTED_IF(instr.suatom_d.is_ba != 0);

        Node value = GetRegister(instr.gpr0);

        std::vector<Node> coords;
        const std::size_t num_coords{GetImageTypeNumCoordinates(instr.sust.image_type)};
        for (std::size_t i = 0; i < num_coords; ++i) {
            coords.push_back(GetRegister(instr.gpr8.Value() + i));
        }

        const OperationCode operation_code = [instr] {
            switch (instr.suatom_d.operation) {
            case Tegra::Shader::ImageAtomicOperation::Add:
                return OperationCode::AtomicImageAdd;
            case Tegra::Shader::ImageAtomicOperation::Min:
                return OperationCode::AtomicImageMin;
            case Tegra::Shader::ImageAtomicOperation::Max:
                return OperationCode::AtomicImageMax;
            case Tegra::Shader::ImageAtomicOperation::And:
                return OperationCode::AtomicImageAnd;
            case Tegra::Shader::ImageAtomicOperation::Or:
                return OperationCode::AtomicImageOr;
            case Tegra::Shader::ImageAtomicOperation::Xor:
                return OperationCode::AtomicImageXor;
            case Tegra::Shader::ImageAtomicOperation::Exch:
                return OperationCode::AtomicImageExchange;
            default:
                UNIMPLEMENTED_MSG("Unimplemented operation={}",
                                  static_cast<u32>(instr.suatom_d.operation.Value()));
                return OperationCode::AtomicImageAdd;
            }
        }();

        const auto& image{GetImage(instr.image, instr.suatom_d.image_type, instr.suatom_d.size)};
        MetaImage meta{image, {std::move(value)}};
        SetRegister(bb, instr.gpr0, Operation(operation_code, meta, std::move(coords)));
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled image instruction: {}", opcode->get().GetName());
    }

    return pc;
}

Image& ShaderIR::GetImage(Tegra::Shader::Image image, Tegra::Shader::ImageType type,
                          std::optional<Tegra::Shader::ImageAtomicSize> size) {
    const auto offset{static_cast<std::size_t>(image.index.Value())};
    if (const auto image = TryUseExistingImage(offset, type, size)) {
        return *image;
    }

    const std::size_t next_index{used_images.size()};
    return used_images.emplace(offset, Image{offset, next_index, type, size}).first->second;
}

Image& ShaderIR::GetBindlessImage(Tegra::Shader::Register reg, Tegra::Shader::ImageType type,
                                  std::optional<Tegra::Shader::ImageAtomicSize> size) {
    const Node image_register{GetRegister(reg)};
    const auto [base_image, cbuf_index, cbuf_offset]{
        TrackCbuf(image_register, global_code, static_cast<s64>(global_code.size()))};
    const auto cbuf_key{(static_cast<u64>(cbuf_index) << 32) | static_cast<u64>(cbuf_offset)};

    if (const auto image = TryUseExistingImage(cbuf_key, type, size)) {
        return *image;
    }

    const std::size_t next_index{used_images.size()};
    return used_images.emplace(cbuf_key, Image{cbuf_index, cbuf_offset, next_index, type, size})
        .first->second;
}

Image* ShaderIR::TryUseExistingImage(u64 offset, Tegra::Shader::ImageType type,
                                     std::optional<Tegra::Shader::ImageAtomicSize> size) {
    auto it = used_images.find(offset);
    if (it == used_images.end()) {
        return nullptr;
    }
    auto& image = it->second;
    ASSERT(image.GetType() == type);

    if (size) {
        // We know the size, if it's known it has to be the same as before, otherwise we can set it.
        if (image.IsSizeKnown()) {
            ASSERT(image.GetSize() == size);
        } else {
            image.SetSize(*size);
        }
    }
    return &image;
}

} // namespace VideoCommon::Shader
