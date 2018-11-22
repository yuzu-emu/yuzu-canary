// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <glad/glad.h>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/maxwell_3d.h"

namespace OpenGL {

using GLvec2 = std::array<GLfloat, 2>;
using GLvec3 = std::array<GLfloat, 3>;
using GLvec4 = std::array<GLfloat, 4>;

using GLuvec2 = std::array<GLuint, 2>;
using GLuvec3 = std::array<GLuint, 3>;
using GLuvec4 = std::array<GLuint, 4>;

namespace MaxwellToGL {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

inline GLenum VertexType(Maxwell::VertexAttribute attrib) {
    switch (attrib.type) {
    case Maxwell::VertexAttribute::Type::UnsignedInt:
    case Maxwell::VertexAttribute::Type::UnsignedNorm: {

        switch (attrib.size) {
        case Maxwell::VertexAttribute::Size::Size_8:
        case Maxwell::VertexAttribute::Size::Size_8_8:
        case Maxwell::VertexAttribute::Size::Size_8_8_8:
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return GL_UNSIGNED_BYTE;
        case Maxwell::VertexAttribute::Size::Size_16:
        case Maxwell::VertexAttribute::Size::Size_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16_16:
            return GL_UNSIGNED_SHORT;
        case Maxwell::VertexAttribute::Size::Size_32:
        case Maxwell::VertexAttribute::Size::Size_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32_32:
            return GL_UNSIGNED_INT;
        case Maxwell::VertexAttribute::Size::Size_10_10_10_2:
            return GL_UNSIGNED_INT_2_10_10_10_REV;
        }

        LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex size={}", attrib.SizeString());
        UNREACHABLE();
        return {};
    }

    case Maxwell::VertexAttribute::Type::SignedInt:
    case Maxwell::VertexAttribute::Type::SignedNorm: {

        switch (attrib.size) {
        case Maxwell::VertexAttribute::Size::Size_8:
        case Maxwell::VertexAttribute::Size::Size_8_8:
        case Maxwell::VertexAttribute::Size::Size_8_8_8:
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return GL_BYTE;
        case Maxwell::VertexAttribute::Size::Size_16:
        case Maxwell::VertexAttribute::Size::Size_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16_16:
            return GL_SHORT;
        case Maxwell::VertexAttribute::Size::Size_32:
        case Maxwell::VertexAttribute::Size::Size_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32_32:
            return GL_INT;
        case Maxwell::VertexAttribute::Size::Size_10_10_10_2:
            return GL_INT_2_10_10_10_REV;
        }

        LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex size={}", attrib.SizeString());
        UNREACHABLE();
        return {};
    }

    case Maxwell::VertexAttribute::Type::Float: {
        switch (attrib.size) {
        case Maxwell::VertexAttribute::Size::Size_16:
        case Maxwell::VertexAttribute::Size::Size_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16_16:
            return GL_HALF_FLOAT;
        case Maxwell::VertexAttribute::Size::Size_32:
        case Maxwell::VertexAttribute::Size::Size_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32:
        case Maxwell::VertexAttribute::Size::Size_32_32_32_32:
            return GL_FLOAT;
        }
    }
    }

    LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex type={}", attrib.TypeString());
    UNREACHABLE();
    return {};
}

inline GLenum IndexFormat(Maxwell::IndexFormat index_format) {
    switch (index_format) {
    case Maxwell::IndexFormat::UnsignedByte:
        return GL_UNSIGNED_BYTE;
    case Maxwell::IndexFormat::UnsignedShort:
        return GL_UNSIGNED_SHORT;
    case Maxwell::IndexFormat::UnsignedInt:
        return GL_UNSIGNED_INT;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented index_format={}", static_cast<u32>(index_format));
    UNREACHABLE();
    return {};
}

inline GLenum PrimitiveTopology(Maxwell::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell::PrimitiveTopology::Points:
        return GL_POINTS;
    case Maxwell::PrimitiveTopology::Lines:
        return GL_LINES;
    case Maxwell::PrimitiveTopology::LineStrip:
        return GL_LINE_STRIP;
    case Maxwell::PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case Maxwell::PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented topology={}", static_cast<u32>(topology));
    UNREACHABLE();
    return {};
}

inline GLenum TextureFilterMode(Tegra::Texture::TextureFilter filter_mode,
                                Tegra::Texture::TextureMipmapFilter mip_filter_mode) {
    switch (filter_mode) {
    case Tegra::Texture::TextureFilter::Linear: {
        switch (mip_filter_mode) {
        case Tegra::Texture::TextureMipmapFilter::None:
            return GL_LINEAR;
        case Tegra::Texture::TextureMipmapFilter::Nearest:
            return GL_NEAREST_MIPMAP_LINEAR;
        case Tegra::Texture::TextureMipmapFilter::Linear:
            return GL_LINEAR_MIPMAP_LINEAR;
        }
    }
    case Tegra::Texture::TextureFilter::Nearest: {
        switch (mip_filter_mode) {
        case Tegra::Texture::TextureMipmapFilter::None:
            return GL_NEAREST;
        case Tegra::Texture::TextureMipmapFilter::Nearest:
            return GL_NEAREST_MIPMAP_NEAREST;
        case Tegra::Texture::TextureMipmapFilter::Linear:
            return GL_LINEAR_MIPMAP_NEAREST;
        }
    }
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented texture filter mode={}", static_cast<u32>(filter_mode));
    return GL_LINEAR;
}

inline GLenum WrapMode(Tegra::Texture::WrapMode wrap_mode) {
    switch (wrap_mode) {
    case Tegra::Texture::WrapMode::Wrap:
        return GL_REPEAT;
    case Tegra::Texture::WrapMode::Mirror:
        return GL_MIRRORED_REPEAT;
    case Tegra::Texture::WrapMode::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
    case Tegra::Texture::WrapMode::Border:
        return GL_CLAMP_TO_BORDER;
    case Tegra::Texture::WrapMode::ClampOGL:
        // TODO(Subv): GL_CLAMP was removed as of OpenGL 3.1, to implement GL_CLAMP, we can use
        // GL_CLAMP_TO_BORDER to get the border color of the texture, and then sample the edge to
        // manually mix them. However the shader part of this is not yet implemented.
        return GL_CLAMP_TO_BORDER;
    case Tegra::Texture::WrapMode::MirrorOnceClampToEdge:
        return GL_MIRROR_CLAMP_TO_EDGE;
    case Tegra::Texture::WrapMode::MirrorOnceBorder:
        if (GL_EXT_texture_mirror_clamp) {
            return GL_MIRROR_CLAMP_TO_BORDER_EXT;
        } else {
            return GL_MIRROR_CLAMP_TO_EDGE;
        }
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented texture wrap mode={}", static_cast<u32>(wrap_mode));
    return GL_REPEAT;
}

inline GLenum DepthCompareFunc(Tegra::Texture::DepthCompareFunc func) {
    switch (func) {
    case Tegra::Texture::DepthCompareFunc::Never:
        return GL_NEVER;
    case Tegra::Texture::DepthCompareFunc::Less:
        return GL_LESS;
    case Tegra::Texture::DepthCompareFunc::LessEqual:
        return GL_LEQUAL;
    case Tegra::Texture::DepthCompareFunc::Equal:
        return GL_EQUAL;
    case Tegra::Texture::DepthCompareFunc::NotEqual:
        return GL_NOTEQUAL;
    case Tegra::Texture::DepthCompareFunc::Greater:
        return GL_GREATER;
    case Tegra::Texture::DepthCompareFunc::GreaterEqual:
        return GL_GEQUAL;
    case Tegra::Texture::DepthCompareFunc::Always:
        return GL_ALWAYS;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented texture depth compare function ={}",
              static_cast<u32>(func));
    return GL_GREATER;
}

inline GLenum BlendEquation(Maxwell::Blend::Equation equation) {
    switch (equation) {
    case Maxwell::Blend::Equation::Add:
    case Maxwell::Blend::Equation::AddGL:
        return GL_FUNC_ADD;
    case Maxwell::Blend::Equation::Subtract:
    case Maxwell::Blend::Equation::SubtractGL:
        return GL_FUNC_SUBTRACT;
    case Maxwell::Blend::Equation::ReverseSubtract:
    case Maxwell::Blend::Equation::ReverseSubtractGL:
        return GL_FUNC_REVERSE_SUBTRACT;
    case Maxwell::Blend::Equation::Min:
    case Maxwell::Blend::Equation::MinGL:
        return GL_MIN;
    case Maxwell::Blend::Equation::Max:
    case Maxwell::Blend::Equation::MaxGL:
        return GL_MAX;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented blend equation={}", static_cast<u32>(equation));
    return GL_FUNC_ADD;
}

inline GLenum BlendFunc(Maxwell::Blend::Factor factor) {
    switch (factor) {
    case Maxwell::Blend::Factor::Zero:
    case Maxwell::Blend::Factor::ZeroGL:
        return GL_ZERO;
    case Maxwell::Blend::Factor::One:
    case Maxwell::Blend::Factor::OneGL:
        return GL_ONE;
    case Maxwell::Blend::Factor::SourceColor:
    case Maxwell::Blend::Factor::SourceColorGL:
        return GL_SRC_COLOR;
    case Maxwell::Blend::Factor::OneMinusSourceColor:
    case Maxwell::Blend::Factor::OneMinusSourceColorGL:
        return GL_ONE_MINUS_SRC_COLOR;
    case Maxwell::Blend::Factor::SourceAlpha:
    case Maxwell::Blend::Factor::SourceAlphaGL:
        return GL_SRC_ALPHA;
    case Maxwell::Blend::Factor::OneMinusSourceAlpha:
    case Maxwell::Blend::Factor::OneMinusSourceAlphaGL:
        return GL_ONE_MINUS_SRC_ALPHA;
    case Maxwell::Blend::Factor::DestAlpha:
    case Maxwell::Blend::Factor::DestAlphaGL:
        return GL_DST_ALPHA;
    case Maxwell::Blend::Factor::OneMinusDestAlpha:
    case Maxwell::Blend::Factor::OneMinusDestAlphaGL:
        return GL_ONE_MINUS_DST_ALPHA;
    case Maxwell::Blend::Factor::DestColor:
    case Maxwell::Blend::Factor::DestColorGL:
        return GL_DST_COLOR;
    case Maxwell::Blend::Factor::OneMinusDestColor:
    case Maxwell::Blend::Factor::OneMinusDestColorGL:
        return GL_ONE_MINUS_DST_COLOR;
    case Maxwell::Blend::Factor::SourceAlphaSaturate:
    case Maxwell::Blend::Factor::SourceAlphaSaturateGL:
        return GL_SRC_ALPHA_SATURATE;
    case Maxwell::Blend::Factor::Source1Color:
    case Maxwell::Blend::Factor::Source1ColorGL:
        return GL_SRC1_COLOR;
    case Maxwell::Blend::Factor::OneMinusSource1Color:
    case Maxwell::Blend::Factor::OneMinusSource1ColorGL:
        return GL_ONE_MINUS_SRC1_COLOR;
    case Maxwell::Blend::Factor::Source1Alpha:
    case Maxwell::Blend::Factor::Source1AlphaGL:
        return GL_SRC1_ALPHA;
    case Maxwell::Blend::Factor::OneMinusSource1Alpha:
    case Maxwell::Blend::Factor::OneMinusSource1AlphaGL:
        return GL_ONE_MINUS_SRC1_ALPHA;
    case Maxwell::Blend::Factor::ConstantColor:
    case Maxwell::Blend::Factor::ConstantColorGL:
        return GL_CONSTANT_COLOR;
    case Maxwell::Blend::Factor::OneMinusConstantColor:
    case Maxwell::Blend::Factor::OneMinusConstantColorGL:
        return GL_ONE_MINUS_CONSTANT_COLOR;
    case Maxwell::Blend::Factor::ConstantAlpha:
    case Maxwell::Blend::Factor::ConstantAlphaGL:
        return GL_CONSTANT_ALPHA;
    case Maxwell::Blend::Factor::OneMinusConstantAlpha:
    case Maxwell::Blend::Factor::OneMinusConstantAlphaGL:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented blend factor={}", static_cast<u32>(factor));
    return GL_ZERO;
}

inline GLenum SwizzleSource(Tegra::Texture::SwizzleSource source) {
    switch (source) {
    case Tegra::Texture::SwizzleSource::Zero:
        return GL_ZERO;
    case Tegra::Texture::SwizzleSource::R:
        return GL_RED;
    case Tegra::Texture::SwizzleSource::G:
        return GL_GREEN;
    case Tegra::Texture::SwizzleSource::B:
        return GL_BLUE;
    case Tegra::Texture::SwizzleSource::A:
        return GL_ALPHA;
    case Tegra::Texture::SwizzleSource::OneInt:
    case Tegra::Texture::SwizzleSource::OneFloat:
        return GL_ONE;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented swizzle source={}", static_cast<u32>(source));
    return GL_ZERO;
}

inline GLenum ComparisonOp(Maxwell::ComparisonOp comparison) {
    switch (comparison) {
    case Maxwell::ComparisonOp::Never:
    case Maxwell::ComparisonOp::NeverOld:
        return GL_NEVER;
    case Maxwell::ComparisonOp::Less:
    case Maxwell::ComparisonOp::LessOld:
        return GL_LESS;
    case Maxwell::ComparisonOp::Equal:
    case Maxwell::ComparisonOp::EqualOld:
        return GL_EQUAL;
    case Maxwell::ComparisonOp::LessEqual:
    case Maxwell::ComparisonOp::LessEqualOld:
        return GL_LEQUAL;
    case Maxwell::ComparisonOp::Greater:
    case Maxwell::ComparisonOp::GreaterOld:
        return GL_GREATER;
    case Maxwell::ComparisonOp::NotEqual:
    case Maxwell::ComparisonOp::NotEqualOld:
        return GL_NOTEQUAL;
    case Maxwell::ComparisonOp::GreaterEqual:
    case Maxwell::ComparisonOp::GreaterEqualOld:
        return GL_GEQUAL;
    case Maxwell::ComparisonOp::Always:
    case Maxwell::ComparisonOp::AlwaysOld:
        return GL_ALWAYS;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented comparison op={}", static_cast<u32>(comparison));
    return GL_ALWAYS;
}

inline GLenum StencilOp(Maxwell::StencilOp stencil) {
    switch (stencil) {
    case Maxwell::StencilOp::Keep:
    case Maxwell::StencilOp::KeepOGL:
        return GL_KEEP;
    case Maxwell::StencilOp::Zero:
    case Maxwell::StencilOp::ZeroOGL:
        return GL_ZERO;
    case Maxwell::StencilOp::Replace:
    case Maxwell::StencilOp::ReplaceOGL:
        return GL_REPLACE;
    case Maxwell::StencilOp::Incr:
    case Maxwell::StencilOp::IncrOGL:
        return GL_INCR;
    case Maxwell::StencilOp::Decr:
    case Maxwell::StencilOp::DecrOGL:
        return GL_DECR;
    case Maxwell::StencilOp::Invert:
    case Maxwell::StencilOp::InvertOGL:
        return GL_INVERT;
    case Maxwell::StencilOp::IncrWrap:
    case Maxwell::StencilOp::IncrWrapOGL:
        return GL_INCR_WRAP;
    case Maxwell::StencilOp::DecrWrap:
    case Maxwell::StencilOp::DecrWrapOGL:
        return GL_DECR_WRAP;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented stencil op={}", static_cast<u32>(stencil));
    return GL_KEEP;
}

inline GLenum FrontFace(Maxwell::Cull::FrontFace front_face) {
    switch (front_face) {
    case Maxwell::Cull::FrontFace::ClockWise:
        return GL_CW;
    case Maxwell::Cull::FrontFace::CounterClockWise:
        return GL_CCW;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented front face cull={}", static_cast<u32>(front_face));
    return GL_CCW;
}

inline GLenum CullFace(Maxwell::Cull::CullFace cull_face) {
    switch (cull_face) {
    case Maxwell::Cull::CullFace::Front:
        return GL_FRONT;
    case Maxwell::Cull::CullFace::Back:
        return GL_BACK;
    case Maxwell::Cull::CullFace::FrontAndBack:
        return GL_FRONT_AND_BACK;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented cull face={}", static_cast<u32>(cull_face));
    return GL_BACK;
}

inline GLenum LogicOp(Maxwell::LogicOperation operation) {
    switch (operation) {
    case Maxwell::LogicOperation::Clear:
        return GL_CLEAR;
    case Maxwell::LogicOperation::And:
        return GL_AND;
    case Maxwell::LogicOperation::AndReverse:
        return GL_AND_REVERSE;
    case Maxwell::LogicOperation::Copy:
        return GL_COPY;
    case Maxwell::LogicOperation::AndInverted:
        return GL_AND_INVERTED;
    case Maxwell::LogicOperation::NoOp:
        return GL_NOOP;
    case Maxwell::LogicOperation::Xor:
        return GL_XOR;
    case Maxwell::LogicOperation::Or:
        return GL_OR;
    case Maxwell::LogicOperation::Nor:
        return GL_NOR;
    case Maxwell::LogicOperation::Equiv:
        return GL_EQUIV;
    case Maxwell::LogicOperation::Invert:
        return GL_INVERT;
    case Maxwell::LogicOperation::OrReverse:
        return GL_OR_REVERSE;
    case Maxwell::LogicOperation::CopyInverted:
        return GL_COPY_INVERTED;
    case Maxwell::LogicOperation::OrInverted:
        return GL_OR_INVERTED;
    case Maxwell::LogicOperation::Nand:
        return GL_NAND;
    case Maxwell::LogicOperation::Set:
        return GL_SET;
    }
    LOG_ERROR(Render_OpenGL, "Unimplemented logic operation={}", static_cast<u32>(operation));
    return GL_COPY;
}

} // namespace MaxwellToGL
} // namespace OpenGL
