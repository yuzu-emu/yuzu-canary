// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <functional>
#include <string>
#include <boost/optional.hpp>
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace GLShader {
namespace Decompiler {

using Tegra::Engines::Maxwell3D;

std::string GetCommonDeclarations();

boost::optional<ProgramResult> DecompileProgram(const ProgramCode& program_code, u32 main_offset,
                                                Maxwell3D::Regs::ShaderStage stage,
                                                const std::string& suffix);

} // namespace Decompiler
} // namespace GLShader
