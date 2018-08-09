// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <glad/glad.h>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/maxwell_3d.h"

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
    case Maxwell::VertexAttribute::Type::UnsignedNorm: {

        switch (attrib.size) {
        case Maxwell::VertexAttribute::Size::Size_8_8:
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return GL_UNSIGNED_BYTE;
        case Maxwell::VertexAttribute::Size::Size_16_16:
        case Maxwell::VertexAttribute::Size::Size_16_16_16_16:
            return GL_UNSIGNED_SHORT;
        case Maxwell::VertexAttribute::Size::Size_10_10_10_2:
            return GL_UNSIGNED_INT_2_10_10_10_REV;
        }

        LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex size={}", attrib.SizeString());
        UNREACHABLE();
        return {};
    }

    case Maxwell::VertexAttribute::Type::SignedNorm: {

        switch (attrib.size) {
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return GL_BYTE;
        case Maxwell::VertexAttribute::Size::Size_16_16:
            return GL_SHORT;
        case Maxwell::VertexAttribute::Size::Size_10_10_10_2:
            return GL_INT_2_10_10_10_REV;
        }

        LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex size={}", attrib.SizeString());
        UNREACHABLE();
        return {};
    }

    case Maxwell::VertexAttribute::Type::UnsignedInt:
        return GL_UNSIGNED_INT;

    case Maxwell::VertexAttribute::Type::Float:
        return GL_FLOAT;
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
    case Maxwell::PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case Maxwell::PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented topology={}", static_cast<u32>(topology));
    UNREACHABLE();
    return {};
}

inline GLenum TextureFilterMode(Tegra::Texture::TextureFilter filter_mode) {
    switch (filter_mode) {
    case Tegra::Texture::TextureFilter::Linear:
        return GL_LINEAR;
    case Tegra::Texture::TextureFilter::Nearest:
        return GL_NEAREST;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented texture filter mode={}",
                 static_cast<u32>(filter_mode));
    UNREACHABLE();
    return {};
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
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented texture wrap mode={}", static_cast<u32>(wrap_mode));
    UNREACHABLE();
    return {};
}

inline GLenum BlendEquation(Maxwell::Blend::Equation equation) {
    switch (equation) {
    case Maxwell::Blend::Equation::Add:
        return GL_FUNC_ADD;
    case Maxwell::Blend::Equation::Subtract:
        return GL_FUNC_SUBTRACT;
    case Maxwell::Blend::Equation::ReverseSubtract:
        return GL_FUNC_REVERSE_SUBTRACT;
    case Maxwell::Blend::Equation::Min:
        return GL_MIN;
    case Maxwell::Blend::Equation::Max:
        return GL_MAX;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented blend equation={}", static_cast<u32>(equation));
    UNREACHABLE();
    return {};
}

inline GLenum BlendFunc(Maxwell::Blend::Factor factor) {
    switch (factor) {
    case Maxwell::Blend::Factor::Zero:
        return GL_ZERO;
    case Maxwell::Blend::Factor::One:
        return GL_ONE;
    case Maxwell::Blend::Factor::SourceColor:
        return GL_SRC_COLOR;
    case Maxwell::Blend::Factor::OneMinusSourceColor:
        return GL_ONE_MINUS_SRC_COLOR;
    case Maxwell::Blend::Factor::SourceAlpha:
        return GL_SRC_ALPHA;
    case Maxwell::Blend::Factor::OneMinusSourceAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case Maxwell::Blend::Factor::DestAlpha:
        return GL_DST_ALPHA;
    case Maxwell::Blend::Factor::OneMinusDestAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
    case Maxwell::Blend::Factor::DestColor:
        return GL_DST_COLOR;
    case Maxwell::Blend::Factor::OneMinusDestColor:
        return GL_ONE_MINUS_DST_COLOR;
    case Maxwell::Blend::Factor::SourceAlphaSaturate:
        return GL_SRC_ALPHA_SATURATE;
    case Maxwell::Blend::Factor::Source1Color:
        return GL_SRC1_COLOR;
    case Maxwell::Blend::Factor::OneMinusSource1Color:
        return GL_ONE_MINUS_SRC1_COLOR;
    case Maxwell::Blend::Factor::Source1Alpha:
        return GL_SRC1_ALPHA;
    case Maxwell::Blend::Factor::OneMinusSource1Alpha:
        return GL_ONE_MINUS_SRC1_ALPHA;
    case Maxwell::Blend::Factor::ConstantColor:
        return GL_CONSTANT_COLOR;
    case Maxwell::Blend::Factor::OneMinusConstantColor:
        return GL_ONE_MINUS_CONSTANT_COLOR;
    case Maxwell::Blend::Factor::ConstantAlpha:
        return GL_CONSTANT_ALPHA;
    case Maxwell::Blend::Factor::OneMinusConstantAlpha:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented blend factor={}", static_cast<u32>(factor));
    UNREACHABLE();
    return {};
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented swizzle source={}", static_cast<u32>(source));
    UNREACHABLE();
    return {};
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented comparison op={}", static_cast<u32>(comparison));
    UNREACHABLE();
    return {};
}

inline GLenum FrontFace(Maxwell::Cull::FrontFace front_face) {
    switch (front_face) {
    case Maxwell::Cull::FrontFace::ClockWise:
        return GL_CW;
    case Maxwell::Cull::FrontFace::CounterClockWise:
        return GL_CCW;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented front face cull={}", static_cast<u32>(front_face));
    UNREACHABLE();
    return {};
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented cull face={}", static_cast<u32>(cull_face));
    UNREACHABLE();
    return {};
}

} // namespace MaxwellToGL
