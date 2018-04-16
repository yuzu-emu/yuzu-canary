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
        case Maxwell::VertexAttribute::Size::Size_8_8_8_8:
            return GL_UNSIGNED_BYTE;
        }

        LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex size=%s", attrib.SizeString().c_str());
        UNREACHABLE();
        return {};
    }

    case Maxwell::VertexAttribute::Type::Float:
        return GL_FLOAT;
    }

    LOG_CRITICAL(Render_OpenGL, "Unimplemented vertex type=%s", attrib.TypeString().c_str());
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented index_format=%d", index_format);
    UNREACHABLE();
    return {};
}

inline GLenum PrimitiveTopology(Maxwell::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell::PrimitiveTopology::Triangles:
        return GL_TRIANGLES;
    case Maxwell::PrimitiveTopology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented topology=%d", topology);
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented texture filter mode=%u",
                 static_cast<u32>(filter_mode));
    UNREACHABLE();
    return {};
}

inline GLenum WrapMode(Tegra::Texture::WrapMode wrap_mode) {
    switch (wrap_mode) {
    case Tegra::Texture::WrapMode::Wrap:
        return GL_REPEAT;
    case Tegra::Texture::WrapMode::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
    }
    LOG_CRITICAL(Render_OpenGL, "Unimplemented texture wrap mode=%u", static_cast<u32>(wrap_mode));
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented blend equation=%u", static_cast<u32>(equation));
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
    LOG_CRITICAL(Render_OpenGL, "Unimplemented blend factor=%u", static_cast<u32>(factor));
    UNREACHABLE();
    return {};
}

} // namespace MaxwellToGL
