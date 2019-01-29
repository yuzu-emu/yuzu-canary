// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_shader_gen.h"

namespace Core {
class System;
}

namespace FileUtil {
class IOFile;
}

namespace OpenGL {

using ProgramCode = std::vector<u64>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;

/// Allocated bindings used by an OpenGL shader program
struct BaseBindings {
    u32 cbuf{};
    u32 gmem{};
    u32 sampler{};

    bool operator<(const BaseBindings& rhs) const {
        return Tie() < rhs.Tie();
    }

    bool operator==(const BaseBindings& rhs) const {
        return Tie() == rhs.Tie();
    }

    bool operator!=(const BaseBindings& rhs) const {
        return !operator==(rhs);
    }

    std::tuple<u32, u32, u32> Tie() const {
        return std::tie(cbuf, gmem, sampler);
    }
};

/// Describes a shader how it's used by the guest GPU
class ShaderDiskCacheRaw {
public:
    explicit ShaderDiskCacheRaw(u64 unique_identifier, Maxwell::ShaderProgram program_type,
                                u32 program_code_size, u32 program_code_size_b,
                                ProgramCode program_code, ProgramCode program_code_b);
    ShaderDiskCacheRaw();
    ~ShaderDiskCacheRaw();

    bool Load(FileUtil::IOFile& file);

    bool Save(FileUtil::IOFile& file) const;

    u64 GetUniqueIdentifier() const {
        return unique_identifier;
    }

    bool HasProgramA() const {
        return program_type == Maxwell::ShaderProgram::VertexA;
    }

    Maxwell::ShaderProgram GetProgramType() const {
        return program_type;
    }

    Maxwell::ShaderStage GetProgramStage() const {
        switch (program_type) {
        case Maxwell::ShaderProgram::VertexA:
        case Maxwell::ShaderProgram::VertexB:
            return Maxwell::ShaderStage::Vertex;
        case Maxwell::ShaderProgram::TesselationControl:
            return Maxwell::ShaderStage::TesselationControl;
        case Maxwell::ShaderProgram::TesselationEval:
            return Maxwell::ShaderStage::TesselationEval;
        case Maxwell::ShaderProgram::Geometry:
            return Maxwell::ShaderStage::Geometry;
        case Maxwell::ShaderProgram::Fragment:
            return Maxwell::ShaderStage::Fragment;
        }
        UNREACHABLE();
    }

    const ProgramCode& GetProgramCode() const {
        return program_code;
    }

    const ProgramCode& GetProgramCodeB() const {
        return program_code_b;
    }

private:
    u64 unique_identifier{};
    Maxwell::ShaderProgram program_type{};
    u32 program_code_size{};
    u32 program_code_size_b{};

    ProgramCode program_code;
    ProgramCode program_code_b;
};

/// Describes how a shader is used
struct ShaderDiskCacheUsage {
    bool operator<(const ShaderDiskCacheUsage& rhs) const {
        return Tie() < rhs.Tie();
    }

    bool operator==(const ShaderDiskCacheUsage& rhs) const {
        return Tie() == rhs.Tie();
    }

    bool operator!=(const ShaderDiskCacheUsage& rhs) const {
        return !operator==(rhs);
    }

    u64 unique_identifier{};
    BaseBindings bindings;
    GLenum primitive{};

private:
    std::tuple<u64, BaseBindings, GLenum> Tie() const {
        return std::tie(unique_identifier, bindings, primitive);
    }
};

/// Contains decompiled data from a shader
struct ShaderDiskCacheDecompiled {
    std::string code;
    GLShader::ShaderEntries entries;
};

/// Contains an OpenGL dumped binary program
struct ShaderDiskCacheDump {
    GLenum binary_format;
    std::vector<u8> binary;
};

class ShaderDiskCacheOpenGL {
public:
    explicit ShaderDiskCacheOpenGL(Core::System& system);

    /// Loads transferable cache. If file has a old version or on failure, it deletes the file.
    std::optional<std::pair<std::vector<ShaderDiskCacheRaw>, std::vector<ShaderDiskCacheUsage>>>
    LoadTransferable();

    /// Loads current game's precompiled cache. Invalidates on failure.
    std::pair<std::map<u64, ShaderDiskCacheDecompiled>,
              std::map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>
    LoadPrecompiled();

    /// Removes the transferable (and precompiled) cache file.
    void InvalidateTransferable() const;

    /// Removes the precompiled cache file.
    void InvalidatePrecompiled() const;

    /// Saves a raw dump to the transferable file. Checks for collisions.
    void SaveRaw(const ShaderDiskCacheRaw& entry);

    /// Saves shader usage to the transferable file. Does not check for collisions.
    void SaveUsage(const ShaderDiskCacheUsage& usage);

    /// Saves a decompiled entry to the precompiled file. Does not check for collisions.
    void SaveDecompiled(u64 unique_identifier, const std::string& code,
                        const GLShader::ShaderEntries& entries);

    /// Saves a dump entry to the precompiled file. Does not check for collisions.
    void SaveDump(const ShaderDiskCacheUsage& usage, GLuint program);

private:
    /// Loads the transferable cache. Returns empty on failure.
    std::optional<std::pair<std::map<u64, ShaderDiskCacheDecompiled>,
                            std::map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>>
    LoadPrecompiledFile(FileUtil::IOFile& file);

    /// Loads a decompiled cache entry from the passed file. Returns empty on failure.
    std::optional<ShaderDiskCacheDecompiled> LoadDecompiledEntry(FileUtil::IOFile& file);

    /// Saves a decompiled entry to the passed file. Returns true on success.
    bool SaveDecompiledFile(FileUtil::IOFile& file, u64 unique_identifier, const std::string& code,
                            const std::vector<u8>& compressed_code,
                            const GLShader::ShaderEntries& entries);

    /// Returns if the cache can be used
    bool IsUsable() const;

    /// Opens current game's transferable file and write it's header if it doesn't exist
    FileUtil::IOFile AppendTransferableFile() const;

    /// Opens current game's precompiled file and write it's header if it doesn't exist
    FileUtil::IOFile AppendPrecompiledFile() const;

    /// Create shader disk cache directories. Returns true on success.
    bool EnsureDirectories() const;

    /// Gets current game's transferable file path
    std::string GetTransferablePath() const;

    /// Gets current game's precompiled file path
    std::string GetPrecompiledPath() const;

    /// Get user's transferable directory path
    std::string GetTransferableDir() const;

    /// Get user's precompiled directory path
    std::string GetPrecompiledDir() const;

    /// Get user's shader directory path
    std::string GetBaseDir() const;

    /// Get current game's title id
    std::string GetTitleID() const;

    // Copre system
    Core::System& system;
    // Stored transferable shaders
    std::map<u64, std::set<ShaderDiskCacheUsage>> transferable;
    // The cache has been loaded at boot
    bool tried_to_load{};
};

} // namespace OpenGL