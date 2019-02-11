// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/virtual_file_util.h"
#include "common/zstd_compression.h"

#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/settings.h"

#include "video_core/renderer_opengl/gl_shader_cache.h"
#include "video_core/renderer_opengl/gl_shader_disk_cache.h"

namespace OpenGL {

using ShaderCacheVersionHash = std::array<u8, 64>;

enum class TransferableEntryKind : u32 {
    Raw,
    Usage,
};

enum class PrecompiledEntryKind : u32 {
    Decompiled,
    Dump,
};

constexpr u32 NativeVersion = 1;

// Making sure sizes doesn't change by accident
static_assert(sizeof(BaseBindings) == 12);
static_assert(sizeof(ShaderDiskCacheUsage) == 24);

namespace {

ShaderCacheVersionHash GetShaderCacheVersionHash() {
    ShaderCacheVersionHash hash{};
    const std::size_t length = std::min(std::strlen(Common::g_shader_cache_version), hash.size());
    std::memcpy(hash.data(), Common::g_shader_cache_version, length);
    return hash;
}

} // namespace

ShaderDiskCacheRaw::ShaderDiskCacheRaw(u64 unique_identifier, Maxwell::ShaderProgram program_type,
                                       u32 program_code_size, u32 program_code_size_b,
                                       ProgramCode program_code, ProgramCode program_code_b)
    : unique_identifier{unique_identifier}, program_type{program_type},
      program_code_size{program_code_size}, program_code_size_b{program_code_size_b},
      program_code{std::move(program_code)}, program_code_b{std::move(program_code_b)} {}

ShaderDiskCacheRaw::ShaderDiskCacheRaw() = default;

ShaderDiskCacheRaw::~ShaderDiskCacheRaw() = default;

bool ShaderDiskCacheRaw::Load(FileUtil::IOFile& file) {
    if (file.ReadBytes(&unique_identifier, sizeof(u64)) != sizeof(u64) ||
        file.ReadBytes(&program_type, sizeof(u32)) != sizeof(u32)) {
        return false;
    }
    u32 program_code_size{};
    u32 program_code_size_b{};
    if (file.ReadBytes(&program_code_size, sizeof(u32)) != sizeof(u32) ||
        file.ReadBytes(&program_code_size_b, sizeof(u32)) != sizeof(u32)) {
        return false;
    }

    program_code.resize(program_code_size);
    program_code_b.resize(program_code_size_b);

    if (file.ReadArray(program_code.data(), program_code_size) != program_code_size)
        return false;

    if (HasProgramA() &&
        file.ReadArray(program_code_b.data(), program_code_size_b) != program_code_size_b) {
        return false;
    }
    return true;
}

bool ShaderDiskCacheRaw::Save(FileUtil::IOFile& file) const {
    if (file.WriteObject(unique_identifier) != 1 ||
        file.WriteObject(static_cast<u32>(program_type)) != 1 ||
        file.WriteObject(program_code_size) != 1 || file.WriteObject(program_code_size_b) != 1) {
        return false;
    }

    if (file.WriteArray(program_code.data(), program_code_size) != program_code_size)
        return false;

    if (HasProgramA() &&
        file.WriteArray(program_code_b.data(), program_code_size_b) != program_code_size_b) {
        return false;
    }
    return true;
}

ShaderDiskCacheOpenGL::ShaderDiskCacheOpenGL(Core::System& system) : system{system} {}

std::optional<std::pair<std::vector<ShaderDiskCacheRaw>, std::vector<ShaderDiskCacheUsage>>>
ShaderDiskCacheOpenGL::LoadTransferable() {
    // Skip games without title id
    const bool has_title_id = system.CurrentProcess()->GetTitleID() != 0;
    if (!Settings::values.use_disk_shader_cache || !has_title_id)
        return {};
    tried_to_load = true;

    FileUtil::IOFile file(GetTransferablePath(), "rb");
    if (!file.IsOpen()) {
        LOG_INFO(Render_OpenGL, "No transferable shader cache found for game with title id={}",
                 GetTitleID());
        return {};
    }

    u32 version{};
    if (file.ReadBytes(&version, sizeof(version)) != sizeof(version)) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to get transferable cache version for title id={} - skipping",
                  GetTitleID());
        return {};
    }

    if (version < NativeVersion) {
        LOG_INFO(Render_OpenGL, "Transferable shader cache is old - removing");
        file.Close();
        InvalidateTransferable();
        return {};
    }
    if (version > NativeVersion) {
        LOG_WARNING(Render_OpenGL, "Transferable shader cache was generated with a newer version "
                                   "of the emulator - skipping");
        return {};
    }

    // Version is valid, load the shaders
    std::vector<ShaderDiskCacheRaw> raws;
    std::vector<ShaderDiskCacheUsage> usages;
    while (file.Tell() < file.GetSize()) {
        TransferableEntryKind kind{};
        if (file.ReadBytes(&kind, sizeof(u32)) != sizeof(u32)) {
            LOG_ERROR(Render_OpenGL, "Failed to read transferable file - skipping");
            return {};
        }

        switch (kind) {
        case TransferableEntryKind::Raw: {
            ShaderDiskCacheRaw entry;
            if (!entry.Load(file)) {
                LOG_ERROR(Render_OpenGL, "Failed to load transferable raw entry - skipping");
                return {};
            }
            transferable.insert({entry.GetUniqueIdentifier(), {}});
            raws.push_back(std::move(entry));
            break;
        }
        case TransferableEntryKind::Usage: {
            ShaderDiskCacheUsage usage{};
            if (file.ReadBytes(&usage, sizeof(usage)) != sizeof(usage)) {
                LOG_ERROR(Render_OpenGL, "Failed to load transferable usage entry - skipping");
                return {};
            }
            usages.push_back(std::move(usage));
            break;
        }
        default:
            LOG_ERROR(Render_OpenGL, "Unknown transferable shader cache entry kind={} - skipping",
                      static_cast<u32>(kind));
            return {};
        }
    }
    return {{raws, usages}};
}

std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>,
          std::unordered_map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>
ShaderDiskCacheOpenGL::LoadPrecompiled() {
    if (!IsUsable())
        return {};

    FileUtil::IOFile file(GetPrecompiledPath(), "rb");
    if (!file.IsOpen()) {
        LOG_INFO(Render_OpenGL, "No precompiled shader cache found for game with title id={}",
                 GetTitleID());
        return {};
    }

    const auto result = LoadPrecompiledFile(file);
    if (!result) {
        LOG_INFO(Render_OpenGL,
                 "Failed to load precompiled cache for game with title id={} - removing",
                 GetTitleID());
        file.Close();
        InvalidatePrecompiled();
        return {};
    }
    return *result;
}

std::optional<std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>,
                        std::unordered_map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>>
ShaderDiskCacheOpenGL::LoadPrecompiledFile(FileUtil::IOFile& file) {

    // Read compressed file from disk and decompress to virtual precompiled cache file
    std::vector<u8> compressed(file.GetSize());
    file.ReadBytes(compressed.data(), compressed.size());
    const std::vector<u8> decompressed = Common::Compression::DecompressDataZSTD(compressed);
    m_precompiled_cache_virtual_file.WriteVector(decompressed);
    m_precompiled_cache_virtual_file.ResetReadWritePosition();

    ShaderCacheVersionHash file_hash{};
    if (m_precompiled_cache_virtual_file.ReadArray(file_hash.data(), file_hash.size()) !=
        file_hash.size()) {
        return {};
    }
    if (GetShaderCacheVersionHash() != file_hash) {
        LOG_INFO(Render_OpenGL, "Precompiled cache is from another version of the emulator");
        return {};
    }

    std::unordered_map<u64, ShaderDiskCacheDecompiled> decompiled;
    std::unordered_map<ShaderDiskCacheUsage, ShaderDiskCacheDump> dumps;
    while (m_precompiled_cache_virtual_file.GetCurrentReadWritePosition() <
           m_precompiled_cache_virtual_file.Size()) {
        PrecompiledEntryKind kind{};
        if (m_precompiled_cache_virtual_file.ReadObject(kind) != 1) {
            return {};
        }

        switch (kind) {
        case PrecompiledEntryKind::Decompiled: {
            u64 unique_identifier{};
            if (m_precompiled_cache_virtual_file.ReadObject(unique_identifier) != 1)
                return {};

            const auto entry = LoadDecompiledEntry();
            if (!entry)
                return {};
            decompiled.insert({unique_identifier, std::move(*entry)});
            break;
        }
        case PrecompiledEntryKind::Dump: {
            ShaderDiskCacheUsage usage;
            if (m_precompiled_cache_virtual_file.ReadObject(usage) != 1)
                return {};

            ShaderDiskCacheDump dump;
            if (m_precompiled_cache_virtual_file.ReadObject(dump.binary_format) != 1)
                return {};

            u32 binary_length{};
            if (m_precompiled_cache_virtual_file.ReadObject(binary_length) != 1) {
                return {};
            }

            dump.binary.resize(binary_length);
            if (m_precompiled_cache_virtual_file.ReadVector(dump.binary) != binary_length) {
                return {};
            }

            dumps.insert({usage, dump});
            break;
        }
        default:
            return {};
        }
    }
    return {{decompiled, dumps}};
}

std::optional<ShaderDiskCacheDecompiled> ShaderDiskCacheOpenGL::LoadDecompiledEntry() {
    u32 code_size{};
    if (m_precompiled_cache_virtual_file.ReadObject(code_size) != 1) {
        return {};
    }

    std::vector<u8> code(code_size);
    if (m_precompiled_cache_virtual_file.ReadVector(code) != code_size) {
        return {};
    }

    ShaderDiskCacheDecompiled entry;
    entry.code = std::string(reinterpret_cast<const char*>(code.data()), code_size);

    u32 const_buffers_count{};
    if (m_precompiled_cache_virtual_file.ReadObject(const_buffers_count) != 1)
        return {};
    for (u32 i = 0; i < const_buffers_count; ++i) {
        u32 max_offset{};
        u32 index{};
        u8 is_indirect{};
        if (m_precompiled_cache_virtual_file.ReadObject(max_offset) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(index) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(is_indirect) != 1) {
            return {};
        }
        entry.entries.const_buffers.emplace_back(max_offset, is_indirect != 0, index);
    }

    u32 samplers_count{};
    if (m_precompiled_cache_virtual_file.ReadObject(samplers_count) != 1)
        return {};
    for (u32 i = 0; i < samplers_count; ++i) {
        u64 offset{};
        u64 index{};
        u32 type{};
        u8 is_array{};
        u8 is_shadow{};
        if (m_precompiled_cache_virtual_file.ReadObject(offset) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(index) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(type) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(is_array) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(is_shadow) != 1) {
            return {};
        }
        entry.entries.samplers.emplace_back(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(index),
            static_cast<Tegra::Shader::TextureType>(type), is_array != 0, is_shadow != 0);
    }

    u32 global_memory_count{};
    if (m_precompiled_cache_virtual_file.ReadObject(global_memory_count) != 1)
        return {};
    for (u32 i = 0; i < global_memory_count; ++i) {
        u32 cbuf_index{};
        u32 cbuf_offset{};
        if (m_precompiled_cache_virtual_file.ReadObject(cbuf_index) != 1 ||
            m_precompiled_cache_virtual_file.ReadObject(cbuf_offset) != 1) {
            return {};
        }
        entry.entries.global_memory_entries.emplace_back(cbuf_index, cbuf_offset);
    }

    for (auto& clip_distance : entry.entries.clip_distances) {
        u8 clip_distance_raw{};
        if (m_precompiled_cache_virtual_file.ReadObject(clip_distance_raw) != 1)
            return {};
        clip_distance = clip_distance_raw != 0;
    }

    u64 shader_length{};
    if (m_precompiled_cache_virtual_file.ReadObject(shader_length) != 1)
        return {};
    entry.entries.shader_length = static_cast<std::size_t>(shader_length);

    return entry;
}

bool ShaderDiskCacheOpenGL::SaveDecompiledFile(u64 unique_identifier, const std::string& code,
                                               const GLShader::ShaderEntries& entries) {
    if (m_precompiled_cache_virtual_file.WriteObject(
            static_cast<u32>(PrecompiledEntryKind::Decompiled)) != 1 ||
        m_precompiled_cache_virtual_file.WriteObject(unique_identifier) != 1 ||
        m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(code.size())) != 1 ||
        m_precompiled_cache_virtual_file.WriteArray(code.data(), code.size()) != code.size()) {
        return false;
    }

    if (m_precompiled_cache_virtual_file.WriteObject(
            static_cast<u32>(entries.const_buffers.size())) != 1)
        return false;
    for (const auto& cbuf : entries.const_buffers) {
        if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(cbuf.GetMaxOffset())) !=
                1 ||
            m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(cbuf.GetIndex())) != 1 ||
            m_precompiled_cache_virtual_file.WriteObject(
                static_cast<u8>(cbuf.IsIndirect() ? 1 : 0)) != 1) {
            return false;
        }
    }

    if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(entries.samplers.size())) !=
        1)
        return false;
    for (const auto& sampler : entries.samplers) {
        if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u64>(sampler.GetOffset())) !=
                1 ||
            m_precompiled_cache_virtual_file.WriteObject(static_cast<u64>(sampler.GetIndex())) !=
                1 ||
            m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(sampler.GetType())) !=
                1 ||
            m_precompiled_cache_virtual_file.WriteObject(
                static_cast<u8>(sampler.IsArray() ? 1 : 0)) != 1 ||
            m_precompiled_cache_virtual_file.WriteObject(
                static_cast<u8>(sampler.IsShadow() ? 1 : 0)) != 1) {
            return false;
        }
    }

    if (m_precompiled_cache_virtual_file.WriteObject(
            static_cast<u32>(entries.global_memory_entries.size())) != 1)
        return false;
    for (const auto& gmem : entries.global_memory_entries) {
        if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(gmem.GetCbufIndex())) !=
                1 ||
            m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(gmem.GetCbufOffset())) !=
                1) {
            return false;
        }
    }

    for (const bool clip_distance : entries.clip_distances) {
        if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u8>(clip_distance ? 1 : 0)) !=
            1)
            return false;
    }

    if (m_precompiled_cache_virtual_file.WriteObject(static_cast<u64>(entries.shader_length)) !=
        1) {
        return false;
    }

    return true;
}

void ShaderDiskCacheOpenGL::InvalidateTransferable() {
    if (!FileUtil::Delete(GetTransferablePath())) {
        LOG_ERROR(Render_OpenGL, "Failed to invalidate transferable file={}",
                  GetTransferablePath());
    }
    InvalidatePrecompiled();
}

void ShaderDiskCacheOpenGL::InvalidatePrecompiled() {
    // Clear virtaul precompiled cache file
    m_precompiled_cache_virtual_file.Resize(0);

    if (!FileUtil::Delete(GetPrecompiledPath())) {
        LOG_ERROR(Render_OpenGL, "Failed to invalidate precompiled file={}", GetPrecompiledPath());
    }
}

void ShaderDiskCacheOpenGL::SaveRaw(const ShaderDiskCacheRaw& entry) {
    if (!IsUsable())
        return;

    const u64 id = entry.GetUniqueIdentifier();
    if (transferable.find(id) != transferable.end()) {
        // The shader already exists
        return;
    }

    FileUtil::IOFile file = AppendTransferableFile();
    if (!file.IsOpen())
        return;
    if (file.WriteObject(TransferableEntryKind::Raw) != 1 || !entry.Save(file)) {
        LOG_ERROR(Render_OpenGL, "Failed to save raw transferable cache entry - removing");
        file.Close();
        InvalidateTransferable();
        return;
    }
    transferable.insert({id, {}});
}

void ShaderDiskCacheOpenGL::SaveUsage(const ShaderDiskCacheUsage& usage) {
    if (!IsUsable())
        return;

    const auto it = transferable.find(usage.unique_identifier);
    ASSERT_MSG(it != transferable.end(), "Saving shader usage without storing raw previously");

    auto& usages{it->second};
    ASSERT(usages.find(usage) == usages.end());
    usages.insert(usage);

    FileUtil::IOFile file = AppendTransferableFile();
    if (!file.IsOpen())
        return;

    if (file.WriteObject(TransferableEntryKind::Usage) != 1 || file.WriteObject(usage) != 1) {
        LOG_ERROR(Render_OpenGL, "Failed to save usage transferable cache entry - removing");
        file.Close();
        InvalidateTransferable();
        return;
    }
}

void ShaderDiskCacheOpenGL::SaveDecompiled(u64 unique_identifier, const std::string& code,
                                           const GLShader::ShaderEntries& entries) {
    if (!IsUsable())
        return;

    if (m_precompiled_cache_virtual_file.Size() == 0) {
        SavePrecompiledHeaderToVirtualPrecompiledCache();
    }

    if (!SaveDecompiledFile(unique_identifier, code, entries)) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to save decompiled entry to the precompiled file - removing");
        InvalidatePrecompiled();
    }
}

void ShaderDiskCacheOpenGL::SaveDump(const ShaderDiskCacheUsage& usage, GLuint program) {
    if (!IsUsable())
        return;

    GLint binary_length{};
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binary_length);

    GLenum binary_format{};
    std::vector<u8> binary(binary_length);
    glGetProgramBinary(program, binary_length, nullptr, &binary_format, binary.data());

    if (m_precompiled_cache_virtual_file.WriteObject(
            static_cast<u32>(PrecompiledEntryKind::Dump)) != 1 ||
        m_precompiled_cache_virtual_file.WriteObject(usage) != 1 ||
        m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(binary_format)) != 1 ||
        m_precompiled_cache_virtual_file.WriteObject(static_cast<u32>(binary_length)) != 1 ||
        m_precompiled_cache_virtual_file.WriteVector(binary) != binary_length) {
        LOG_ERROR(Render_OpenGL, "Failed to save binary program file in shader={:016x} - removing",
                  usage.unique_identifier);
        InvalidatePrecompiled();
        return;
    }
}

bool ShaderDiskCacheOpenGL::IsUsable() const {
    return tried_to_load && Settings::values.use_disk_shader_cache;
}

FileUtil::IOFile ShaderDiskCacheOpenGL::AppendTransferableFile() const {
    if (!EnsureDirectories())
        return {};

    const auto transferable_path{GetTransferablePath()};
    const bool existed = FileUtil::Exists(transferable_path);

    FileUtil::IOFile file(transferable_path, "ab");
    if (!file.IsOpen()) {
        LOG_ERROR(Render_OpenGL, "Failed to open transferable cache in path={}", transferable_path);
        return {};
    }
    if (!existed || file.GetSize() == 0) {
        // If the file didn't exist, write its version
        if (file.WriteObject(NativeVersion) != 1) {
            LOG_ERROR(Render_OpenGL, "Failed to write transferable cache version in path={}",
                      transferable_path);
            return {};
        }
    }
    return file;
}

void ShaderDiskCacheOpenGL::SavePrecompiledHeaderToVirtualPrecompiledCache() {
    const auto hash{GetShaderCacheVersionHash()};
    if (m_precompiled_cache_virtual_file.WriteArray(hash.data(), hash.size()) != hash.size()) {
        LOG_ERROR(
            Render_OpenGL,
            "Failed to write precompiled cache version hash to virtual precompiled cache file");
    }
}

void ShaderDiskCacheOpenGL::SaveVirtualPrecompiledFile() {

    const std::vector<u8> compressed = Common::Compression::CompressDataZSTDDefault(
        m_precompiled_cache_virtual_file.Data(), m_precompiled_cache_virtual_file.Size());

    const auto precompiled_path{GetPrecompiledPath()};
    FileUtil::IOFile file(precompiled_path, "wb");

    if (!file.IsOpen()) {
        LOG_ERROR(Render_OpenGL, "Failed to open precompiled cache in path={}", precompiled_path);
        return;
    }
    if (file.WriteBytes(compressed.data(), compressed.size()) != compressed.size()) {
        LOG_ERROR(Render_OpenGL, "Failed to write precompiled cache version in path={}",
                  precompiled_path);
        return;
    }

    file.Close();
}

bool ShaderDiskCacheOpenGL::EnsureDirectories() const {
    const auto CreateDir = [](const std::string& dir) {
        if (!FileUtil::CreateDir(dir)) {
            LOG_ERROR(Render_OpenGL, "Failed to create directory={}", dir);
            return false;
        }
        return true;
    };

    return CreateDir(FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir)) &&
           CreateDir(GetBaseDir()) && CreateDir(GetTransferableDir()) &&
           CreateDir(GetPrecompiledDir());
}

std::string ShaderDiskCacheOpenGL::GetTransferablePath() const {
    return FileUtil::SanitizePath(GetTransferableDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCacheOpenGL::GetPrecompiledPath() const {
    return FileUtil::SanitizePath(GetPrecompiledDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCacheOpenGL::GetTransferableDir() const {
    return GetBaseDir() + DIR_SEP "transferable";
}

std::string ShaderDiskCacheOpenGL::GetPrecompiledDir() const {
    return GetBaseDir() + DIR_SEP "precompiled";
}

std::string ShaderDiskCacheOpenGL::GetBaseDir() const {
    return FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir) + DIR_SEP "opengl";
}

std::string ShaderDiskCacheOpenGL::GetTitleID() const {
    return fmt::format("{:016X}", system.CurrentProcess()->GetTitleID());
}

} // namespace OpenGL
