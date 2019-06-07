// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_vulkan/declarations.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/surface.h"

namespace Vulkan::MaxwellToVK {

namespace Sampler {

vk::Filter Filter(Tegra::Texture::TextureFilter filter) {
    switch (filter) {
    case Tegra::Texture::TextureFilter::Linear:
        return vk::Filter::eLinear;
    case Tegra::Texture::TextureFilter::Nearest:
        return vk::Filter::eNearest;
    }
    UNIMPLEMENTED_MSG("Unimplemented sampler filter={}", static_cast<u32>(filter));
    return {};
}

vk::SamplerMipmapMode MipmapMode(Tegra::Texture::TextureMipmapFilter mipmap_filter) {
    switch (mipmap_filter) {
    case Tegra::Texture::TextureMipmapFilter::None:
        // TODO(Rodrigo): None seems to be mapped to OpenGL's mag and min filters without mipmapping
        // (e.g. GL_NEAREST and GL_LINEAR). Vulkan doesn't have such a thing, find out if we have to
        // use an image view with a single mipmap level to emulate this.
        return vk::SamplerMipmapMode::eLinear;
    case Tegra::Texture::TextureMipmapFilter::Linear:
        return vk::SamplerMipmapMode::eLinear;
    case Tegra::Texture::TextureMipmapFilter::Nearest:
        return vk::SamplerMipmapMode::eNearest;
    }
    UNIMPLEMENTED_MSG("Unimplemented sampler mipmap mode={}", static_cast<u32>(mipmap_filter));
    return {};
}

vk::SamplerAddressMode WrapMode(Tegra::Texture::WrapMode wrap_mode) {
    switch (wrap_mode) {
    case Tegra::Texture::WrapMode::Wrap:
        return vk::SamplerAddressMode::eRepeat;
    case Tegra::Texture::WrapMode::Mirror:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case Tegra::Texture::WrapMode::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    case Tegra::Texture::WrapMode::Border:
        return vk::SamplerAddressMode::eClampToBorder;
    case Tegra::Texture::WrapMode::Clamp:
        // TODO(Rodrigo): GL_CLAMP was removed as of OpenGL 3.1, to implement GL_CLAMP, we can use
        // eClampToBorder to get the border color of the texture, and then sample the edge to
        // manually mix them. However the shader part of this is not yet implemented.
        return vk::SamplerAddressMode::eClampToBorder;
    case Tegra::Texture::WrapMode::MirrorOnceClampToEdge:
        return vk::SamplerAddressMode::eMirrorClampToEdge;
    case Tegra::Texture::WrapMode::MirrorOnceBorder:
        UNIMPLEMENTED();
        return vk::SamplerAddressMode::eMirrorClampToEdge;
    default:
        UNIMPLEMENTED_MSG("Unimplemented wrap mode={}", static_cast<u32>(wrap_mode));
        return {};
    }
}

vk::CompareOp DepthCompareFunction(Tegra::Texture::DepthCompareFunc depth_compare_func) {
    switch (depth_compare_func) {
    case Tegra::Texture::DepthCompareFunc::Never:
        return vk::CompareOp::eNever;
    case Tegra::Texture::DepthCompareFunc::Less:
        return vk::CompareOp::eLess;
    case Tegra::Texture::DepthCompareFunc::LessEqual:
        return vk::CompareOp::eLessOrEqual;
    case Tegra::Texture::DepthCompareFunc::Equal:
        return vk::CompareOp::eEqual;
    case Tegra::Texture::DepthCompareFunc::NotEqual:
        return vk::CompareOp::eNotEqual;
    case Tegra::Texture::DepthCompareFunc::Greater:
        return vk::CompareOp::eGreater;
    case Tegra::Texture::DepthCompareFunc::GreaterEqual:
        return vk::CompareOp::eGreaterOrEqual;
    case Tegra::Texture::DepthCompareFunc::Always:
        return vk::CompareOp::eAlways;
    }
    UNIMPLEMENTED_MSG("Unimplemented sampler depth compare function={}",
                      static_cast<u32>(depth_compare_func));
    return {};
}

} // namespace Sampler

struct FormatTuple {
    vk::Format format;            ///< Vulkan format
    ComponentType component_type; ///< Abstracted component type
    bool attachable;              ///< True when this format can be used as an attachment
};

static constexpr std::array<FormatTuple, VideoCore::Surface::MaxPixelFormat> tex_format_tuples = {{
    {vk::Format::eA8B8G8R8UnormPack32, ComponentType::UNorm, true},    // ABGR8U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ABGR8S
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ABGR8UI
    {vk::Format::eB5G6R5UnormPack16, ComponentType::UNorm, false},     // B5G6R5U
    {vk::Format::eA2B10G10R10UnormPack32, ComponentType::UNorm, true}, // A2B10G10R10U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // A1B5G5R5U
    {vk::Format::eR8Unorm, ComponentType::UNorm, true},                // R8U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R8UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGBA16F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGBA16U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGBA16UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R11FG11FB10F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGBA32UI
    {vk::Format::eBc1RgbaUnormBlock, ComponentType::UNorm, false},     // DXT1
    {vk::Format::eBc2UnormBlock, ComponentType::UNorm, false},         // DXT23
    {vk::Format::eBc3UnormBlock, ComponentType::UNorm, false},         // DXT45
    {vk::Format::eBc4UnormBlock, ComponentType::UNorm, false},         // DXN1
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // DXN2UNORM
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // DXN2SNORM
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // BC7U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // BC6H_UF16
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // BC6H_SF16
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ASTC_2D_4X4
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // BGRA8
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGBA32F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG32F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R32F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R16F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R16U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R16S
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R16UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R16I
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG16
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG16F
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG16UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG16I
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG16S
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RGB32F
    {vk::Format::eA8B8G8R8SrgbPack32, ComponentType::UNorm, true},     // RGBA8_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG8U
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG8S
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // RG32UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // R32UI
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ASTC_2D_8X8
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ASTC_2D_8X5
    {vk::Format::eUndefined, ComponentType::Invalid, false},           // ASTC_2D_5X4

    // Compressed sRGB formats
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // BGRA8_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // DXT1_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // DXT23_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // DXT45_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // BC7U_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_4X4_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_8X8_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_8X5_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_5X4_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_5X5
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_5X5_SRGB
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_10X8
    {vk::Format::eUndefined, ComponentType::Invalid, false}, // ASTC_2D_10X8_SRGB

    // Depth formats
    {vk::Format::eD32Sfloat, ComponentType::Float, true}, // Z32F
    {vk::Format::eD16Unorm, ComponentType::UNorm, true},  // Z16

    // DepthStencil formats
    {vk::Format::eD24UnormS8Uint, ComponentType::UNorm, true}, // Z24S8
    {vk::Format::eD24UnormS8Uint, ComponentType::UNorm, true}, // S8Z24 (emulated)
    {vk::Format::eUndefined, ComponentType::Invalid, false},   // Z32FS8
}};

static constexpr bool IsZetaFormat(PixelFormat pixel_format) {
    return pixel_format >= PixelFormat::MaxColorFormat &&
           pixel_format < PixelFormat::MaxDepthStencilFormat;
}

std::pair<vk::Format, bool> SurfaceFormat(const VKDevice& device, FormatType format_type,
                                          PixelFormat pixel_format, ComponentType component_type) {
    ASSERT(static_cast<std::size_t>(pixel_format) < tex_format_tuples.size());

    const auto tuple = tex_format_tuples[static_cast<u32>(pixel_format)];
    UNIMPLEMENTED_IF_MSG(tuple.format == vk::Format::eUndefined,
                         "Unimplemented texture format with pixel format={} and component type={}",
                         static_cast<u32>(pixel_format), static_cast<u32>(component_type));
    ASSERT_MSG(component_type == tuple.component_type, "Component type mismatch");

    auto usage = vk::FormatFeatureFlagBits::eSampledImage |
                 vk::FormatFeatureFlagBits::eTransferDst | vk::FormatFeatureFlagBits::eTransferSrc;
    if (tuple.attachable) {
        usage |= IsZetaFormat(pixel_format) ? vk::FormatFeatureFlagBits::eDepthStencilAttachment
                                            : vk::FormatFeatureFlagBits::eColorAttachment;
    }
    return {device.GetSupportedFormat(tuple.format, usage, format_type), tuple.attachable};
}

vk::ShaderStageFlagBits ShaderStage(Maxwell::ShaderStage stage) {
    switch (stage) {
    case Maxwell::ShaderStage::Vertex:
        return vk::ShaderStageFlagBits::eVertex;
    case Maxwell::ShaderStage::TesselationControl:
        return vk::ShaderStageFlagBits::eTessellationControl;
    case Maxwell::ShaderStage::TesselationEval:
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case Maxwell::ShaderStage::Geometry:
        return vk::ShaderStageFlagBits::eGeometry;
    case Maxwell::ShaderStage::Fragment:
        return vk::ShaderStageFlagBits::eFragment;
    }
    UNIMPLEMENTED_MSG("Unimplemented shader stage={}", static_cast<u32>(stage));
    return {};
}

vk::PrimitiveTopology PrimitiveTopology(Maxwell::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell::PrimitiveTopology::Points:
        return vk::PrimitiveTopology::ePointList;
    case Maxwell::PrimitiveTopology::Lines:
        return vk::PrimitiveTopology::eLineList;
    case Maxwell::PrimitiveTopology::LineStrip:
        return vk::PrimitiveTopology::eLineStrip;
    case Maxwell::PrimitiveTopology::Triangles:
        return vk::PrimitiveTopology::eTriangleList;
    case Maxwell::PrimitiveTopology::TriangleStrip:
        return vk::PrimitiveTopology::eTriangleStrip;
    default:
        UNIMPLEMENTED_MSG("Unimplemented topology={}", static_cast<u32>(topology));
        return {};
    }
}

vk::Format VertexFormat(Maxwell::VertexAttribute::Type type, Maxwell::VertexAttribute::Size size) {
    switch (type) {
    case Maxwell::VertexAttribute::Type::SignedNorm:
        break;
    case Maxwell::VertexAttribute::Type::UnsignedNorm:
        switch (size) {
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return vk::Format::eR8G8B8A8Unorm;
        default:
            break;
        }
        break;
    case Maxwell::VertexAttribute::Type::SignedInt:
        break;
    case Maxwell::VertexAttribute::Type::UnsignedInt:
        switch (size) {
        case Maxwell::VertexAttribute::Size::Size_32:
            return vk::Format::eR32Uint;
        default:
            break;
        }
    case Maxwell::VertexAttribute::Type::UnsignedScaled:
    case Maxwell::VertexAttribute::Type::SignedScaled:
        break;
    case Maxwell::VertexAttribute::Type::Float:
        switch (size) {
        case Maxwell::VertexAttribute::Size::Size_32_32_32_32:
            return vk::Format::eR32G32B32A32Sfloat;
        case Maxwell::VertexAttribute::Size::Size_32_32_32:
            return vk::Format::eR32G32B32Sfloat;
        case Maxwell::VertexAttribute::Size::Size_32_32:
            return vk::Format::eR32G32Sfloat;
        case Maxwell::VertexAttribute::Size::Size_32:
            return vk::Format::eR32Sfloat;
        default:
            break;
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented vertex format of type={} and size={}", static_cast<u32>(type),
                      static_cast<u32>(size));
    return {};
}

vk::CompareOp ComparisonOp(Maxwell::ComparisonOp comparison) {
    switch (comparison) {
    case Maxwell::ComparisonOp::Never:
    case Maxwell::ComparisonOp::NeverOld:
        return vk::CompareOp::eNever;
    case Maxwell::ComparisonOp::Less:
    case Maxwell::ComparisonOp::LessOld:
        return vk::CompareOp::eLess;
    case Maxwell::ComparisonOp::Equal:
    case Maxwell::ComparisonOp::EqualOld:
        return vk::CompareOp::eEqual;
    case Maxwell::ComparisonOp::LessEqual:
    case Maxwell::ComparisonOp::LessEqualOld:
        return vk::CompareOp::eLessOrEqual;
    case Maxwell::ComparisonOp::Greater:
    case Maxwell::ComparisonOp::GreaterOld:
        return vk::CompareOp::eGreater;
    case Maxwell::ComparisonOp::NotEqual:
    case Maxwell::ComparisonOp::NotEqualOld:
        return vk::CompareOp::eNotEqual;
    case Maxwell::ComparisonOp::GreaterEqual:
    case Maxwell::ComparisonOp::GreaterEqualOld:
        return vk::CompareOp::eGreaterOrEqual;
    case Maxwell::ComparisonOp::Always:
    case Maxwell::ComparisonOp::AlwaysOld:
        return vk::CompareOp::eAlways;
    }
    UNIMPLEMENTED_MSG("Unimplemented comparison op={}", static_cast<u32>(comparison));
    return {};
}

vk::IndexType IndexFormat(Maxwell::IndexFormat index_format) {
    switch (index_format) {
    case Maxwell::IndexFormat::UnsignedByte:
        UNIMPLEMENTED_MSG("Vulkan does not support native u8 index format");
        return vk::IndexType::eUint16;
    case Maxwell::IndexFormat::UnsignedShort:
        return vk::IndexType::eUint16;
    case Maxwell::IndexFormat::UnsignedInt:
        return vk::IndexType::eUint32;
    }
    UNIMPLEMENTED_MSG("Unimplemented index_format={}", static_cast<u32>(index_format));
    return {};
}

vk::StencilOp StencilOp(Maxwell::StencilOp stencil_op) {
    switch (stencil_op) {
    case Maxwell::StencilOp::Keep:
    case Maxwell::StencilOp::KeepOGL:
        return vk::StencilOp::eKeep;
    case Maxwell::StencilOp::Zero:
    case Maxwell::StencilOp::ZeroOGL:
        return vk::StencilOp::eZero;
    case Maxwell::StencilOp::Replace:
    case Maxwell::StencilOp::ReplaceOGL:
        return vk::StencilOp::eReplace;
    case Maxwell::StencilOp::Incr:
    case Maxwell::StencilOp::IncrOGL:
        return vk::StencilOp::eIncrementAndClamp;
    case Maxwell::StencilOp::Decr:
    case Maxwell::StencilOp::DecrOGL:
        return vk::StencilOp::eDecrementAndClamp;
    case Maxwell::StencilOp::Invert:
    case Maxwell::StencilOp::InvertOGL:
        return vk::StencilOp::eInvert;
    case Maxwell::StencilOp::IncrWrap:
    case Maxwell::StencilOp::IncrWrapOGL:
        return vk::StencilOp::eIncrementAndWrap;
    case Maxwell::StencilOp::DecrWrap:
    case Maxwell::StencilOp::DecrWrapOGL:
        return vk::StencilOp::eDecrementAndWrap;
    }
    UNIMPLEMENTED_MSG("Unimplemented stencil op={}", static_cast<u32>(stencil_op));
    return {};
}

vk::BlendOp BlendEquation(Maxwell::Blend::Equation equation) {
    switch (equation) {
    case Maxwell::Blend::Equation::Add:
    case Maxwell::Blend::Equation::AddGL:
        return vk::BlendOp::eAdd;
    case Maxwell::Blend::Equation::Subtract:
    case Maxwell::Blend::Equation::SubtractGL:
        return vk::BlendOp::eSubtract;
    case Maxwell::Blend::Equation::ReverseSubtract:
    case Maxwell::Blend::Equation::ReverseSubtractGL:
        return vk::BlendOp::eReverseSubtract;
    case Maxwell::Blend::Equation::Min:
    case Maxwell::Blend::Equation::MinGL:
        return vk::BlendOp::eMin;
    case Maxwell::Blend::Equation::Max:
    case Maxwell::Blend::Equation::MaxGL:
        return vk::BlendOp::eMax;
    }
    UNIMPLEMENTED_MSG("Unimplemented blend equation={}", static_cast<u32>(equation));
    return {};
}

vk::BlendFactor BlendFactor(Maxwell::Blend::Factor factor) {
    switch (factor) {
    case Maxwell::Blend::Factor::Zero:
    case Maxwell::Blend::Factor::ZeroGL:
        return vk::BlendFactor::eZero;
    case Maxwell::Blend::Factor::One:
    case Maxwell::Blend::Factor::OneGL:
        return vk::BlendFactor::eOne;
    case Maxwell::Blend::Factor::SourceColor:
    case Maxwell::Blend::Factor::SourceColorGL:
        return vk::BlendFactor::eSrcColor;
    case Maxwell::Blend::Factor::OneMinusSourceColor:
    case Maxwell::Blend::Factor::OneMinusSourceColorGL:
        return vk::BlendFactor::eOneMinusSrcColor;
    case Maxwell::Blend::Factor::SourceAlpha:
    case Maxwell::Blend::Factor::SourceAlphaGL:
        return vk::BlendFactor::eSrcAlpha;
    case Maxwell::Blend::Factor::OneMinusSourceAlpha:
    case Maxwell::Blend::Factor::OneMinusSourceAlphaGL:
        return vk::BlendFactor::eOneMinusSrcAlpha;
    case Maxwell::Blend::Factor::DestAlpha:
    case Maxwell::Blend::Factor::DestAlphaGL:
        return vk::BlendFactor::eDstAlpha;
    case Maxwell::Blend::Factor::OneMinusDestAlpha:
    case Maxwell::Blend::Factor::OneMinusDestAlphaGL:
        return vk::BlendFactor::eOneMinusDstAlpha;
    case Maxwell::Blend::Factor::DestColor:
    case Maxwell::Blend::Factor::DestColorGL:
        return vk::BlendFactor::eDstColor;
    case Maxwell::Blend::Factor::OneMinusDestColor:
    case Maxwell::Blend::Factor::OneMinusDestColorGL:
        return vk::BlendFactor::eOneMinusDstColor;
    case Maxwell::Blend::Factor::SourceAlphaSaturate:
    case Maxwell::Blend::Factor::SourceAlphaSaturateGL:
        return vk::BlendFactor::eSrcAlphaSaturate;
    case Maxwell::Blend::Factor::Source1Color:
    case Maxwell::Blend::Factor::Source1ColorGL:
        return vk::BlendFactor::eSrc1Color;
    case Maxwell::Blend::Factor::OneMinusSource1Color:
    case Maxwell::Blend::Factor::OneMinusSource1ColorGL:
        return vk::BlendFactor::eOneMinusSrc1Color;
    case Maxwell::Blend::Factor::Source1Alpha:
    case Maxwell::Blend::Factor::Source1AlphaGL:
        return vk::BlendFactor::eSrc1Alpha;
    case Maxwell::Blend::Factor::OneMinusSource1Alpha:
    case Maxwell::Blend::Factor::OneMinusSource1AlphaGL:
        return vk::BlendFactor::eOneMinusSrc1Alpha;
    case Maxwell::Blend::Factor::ConstantColor:
    case Maxwell::Blend::Factor::ConstantColorGL:
        return vk::BlendFactor::eConstantColor;
    case Maxwell::Blend::Factor::OneMinusConstantColor:
    case Maxwell::Blend::Factor::OneMinusConstantColorGL:
        return vk::BlendFactor::eOneMinusConstantColor;
    case Maxwell::Blend::Factor::ConstantAlpha:
    case Maxwell::Blend::Factor::ConstantAlphaGL:
        return vk::BlendFactor::eConstantAlpha;
    case Maxwell::Blend::Factor::OneMinusConstantAlpha:
    case Maxwell::Blend::Factor::OneMinusConstantAlphaGL:
        return vk::BlendFactor::eOneMinusConstantAlpha;
    }
    UNIMPLEMENTED_MSG("Unimplemented blend factor={}", static_cast<u32>(factor));
    return {};
}

vk::FrontFace FrontFace(Maxwell::Cull::FrontFace front_face) {
    switch (front_face) {
    case Maxwell::Cull::FrontFace::ClockWise:
        return vk::FrontFace::eClockwise;
    case Maxwell::Cull::FrontFace::CounterClockWise:
        return vk::FrontFace::eCounterClockwise;
    }
    UNIMPLEMENTED_MSG("Unimplemented front face={}", static_cast<u32>(front_face));
    return {};
}

vk::CullModeFlags CullFace(Maxwell::Cull::CullFace cull_face) {
    switch (cull_face) {
    case Maxwell::Cull::CullFace::Front:
        return vk::CullModeFlagBits::eFront;
    case Maxwell::Cull::CullFace::Back:
        return vk::CullModeFlagBits::eBack;
    case Maxwell::Cull::CullFace::FrontAndBack:
        return vk::CullModeFlagBits::eFrontAndBack;
    }
    UNIMPLEMENTED_MSG("Unimplemented cull face={}", static_cast<u32>(cull_face));
    return {};
}

vk::ComponentSwizzle SwizzleSource(Tegra::Texture::SwizzleSource swizzle) {
    switch (swizzle) {
    case Tegra::Texture::SwizzleSource::Zero:
        return vk::ComponentSwizzle::eZero;
    case Tegra::Texture::SwizzleSource::R:
        return vk::ComponentSwizzle::eR;
    case Tegra::Texture::SwizzleSource::G:
        return vk::ComponentSwizzle::eG;
    case Tegra::Texture::SwizzleSource::B:
        return vk::ComponentSwizzle::eB;
    case Tegra::Texture::SwizzleSource::A:
        return vk::ComponentSwizzle::eA;
    case Tegra::Texture::SwizzleSource::OneInt:
    case Tegra::Texture::SwizzleSource::OneFloat:
        return vk::ComponentSwizzle::eOne;
    }
    UNIMPLEMENTED_MSG("Unimplemented swizzle source={}", static_cast<u32>(swizzle));
    return {};
}

} // namespace Vulkan::MaxwellToVK
