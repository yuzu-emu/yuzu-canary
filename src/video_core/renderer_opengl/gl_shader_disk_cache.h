// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/virtual_file_util.h"
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

    bool operator==(const BaseBindings& rhs) const {
        return std::tie(cbuf, gmem, sampler) == std::tie(rhs.cbuf, rhs.gmem, rhs.sampler);
    }

    bool operator!=(const BaseBindings& rhs) const {
        return !operator==(rhs);
    }
};

/// Describes how a shader is used
struct ShaderDiskCacheUsage {
    u64 unique_identifier{};
    BaseBindings bindings;
    GLenum primitive{};

    bool operator==(const ShaderDiskCacheUsage& rhs) const {
        return std::tie(unique_identifier, bindings, primitive) ==
               std::tie(rhs.unique_identifier, rhs.bindings, rhs.primitive);
    }

    bool operator!=(const ShaderDiskCacheUsage& rhs) const {
        return !operator==(rhs);
    }
};

} // namespace OpenGL

namespace std {

template <>
struct hash<OpenGL::BaseBindings> {
    std::size_t operator()(const OpenGL::BaseBindings& bindings) const {
        return bindings.cbuf | bindings.gmem << 8 | bindings.sampler << 16;
    }
};

template <>
struct hash<OpenGL::ShaderDiskCacheUsage> {
    std::size_t operator()(const OpenGL::ShaderDiskCacheUsage& usage) const {
        return static_cast<std::size_t>(usage.unique_identifier) ^
               std::hash<OpenGL::BaseBindings>()(usage.bindings) ^ usage.primitive << 16;
    }
};

} // namespace std

namespace OpenGL {

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
    std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>,
              std::unordered_map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>
    LoadPrecompiled();

    /// Removes the transferable (and precompiled) cache file.
    void InvalidateTransferable();

    /// Removes the precompiled cache file and clears virtual precompiled cache file.
    void InvalidatePrecompiled();

    /// Saves a raw dump to the transferable file. Checks for collisions.
    void SaveRaw(const ShaderDiskCacheRaw& entry);

    /// Saves shader usage to the transferable file. Does not check for collisions.
    void SaveUsage(const ShaderDiskCacheUsage& usage);

    /// Saves a decompiled entry to the precompiled file. Does not check for collisions.
    void SaveDecompiled(u64 unique_identifier, const std::string& code,
                        const GLShader::ShaderEntries& entries);

    /// Saves a dump entry to the precompiled file. Does not check for collisions.
    void SaveDump(const ShaderDiskCacheUsage& usage, GLuint program);

    /// Serializes virtual precompiled shader cache file to real file
    void SaveVirtualPrecompiledFile();

private:
    /// Loads the transferable cache. Returns empty on failure.
    std::optional<std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>,
                            std::unordered_map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>>
    LoadPrecompiledFile(FileUtil::IOFile& file);

    /// Loads a decompiled cache entry from m_precompiled_cache_virtual_file. Returns empty on
    /// failure.
    std::optional<ShaderDiskCacheDecompiled> LoadDecompiledEntry();

    /// Saves a decompiled entry to the passed file. Returns true on success.
    bool SaveDecompiledFile(u64 unique_identifier, const std::string& code,
                            const GLShader::ShaderEntries& entries);

    /// Returns if the cache can be used
    bool IsUsable() const;

    /// Opens current game's transferable file and write it's header if it doesn't exist
    FileUtil::IOFile AppendTransferableFile() const;

    /// Save precompiled header to precompiled_cache_in_memory
    void SavePrecompiledHeaderToVirtualPrecompiledCache();

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
    std::map<u64, std::unordered_set<ShaderDiskCacheUsage>> transferable;
    // stores whole precompiled cache which will be read from or saved to the precompiled chache
    // file
    Common::VirtualFileUtil::VirtualBinaryFile m_precompiled_cache_virtual_file;

    // The cache has been loaded at boot
    bool tried_to_load{};
};

} // namespace OpenGL