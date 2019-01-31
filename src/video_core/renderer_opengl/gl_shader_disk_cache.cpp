// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <fmt/format.h>
#include <lz4.h>

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"

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

template <typename T>
std::vector<u8> CompressData(const T* source, std::size_t source_size) {
    const auto source_size_int = static_cast<int>(source_size);
    const int max_compressed_size = LZ4_compressBound(source_size_int);
    std::vector<u8> compressed(max_compressed_size);
    const int compressed_size = LZ4_compress_default(reinterpret_cast<const char*>(source),
                                                     reinterpret_cast<char*>(compressed.data()),
                                                     source_size_int, max_compressed_size);
    if (compressed_size < 0) {
        // Compression failed
        return {};
    }
    compressed.resize(compressed_size);
    return compressed;
}

std::vector<u8> DecompressData(const std::vector<u8>& compressed, std::size_t uncompressed_size) {
    std::vector<u8> uncompressed(uncompressed_size);
    const int size_check = LZ4_decompress_safe(reinterpret_cast<const char*>(compressed.data()),
                                               reinterpret_cast<char*>(uncompressed.data()),
                                               static_cast<int>(compressed.size()),
                                               static_cast<int>(uncompressed.size()));
    if (static_cast<int>(uncompressed_size) != size_check) {
        // Decompression failed
        return {};
    }
    return uncompressed;
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

std::pair<std::map<u64, ShaderDiskCacheDecompiled>,
          std::map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>
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

std::optional<std::pair<std::map<u64, ShaderDiskCacheDecompiled>,
                        std::map<ShaderDiskCacheUsage, ShaderDiskCacheDump>>>
ShaderDiskCacheOpenGL::LoadPrecompiledFile(FileUtil::IOFile& file) {
    ShaderCacheVersionHash file_hash{};
    if (file.ReadArray(file_hash.data(), file_hash.size()) != file_hash.size()) {
        return {};
    }
    if (GetShaderCacheVersionHash() != file_hash) {
        LOG_INFO(Render_OpenGL, "Precompiled cache is from another version of the emulator");
        return {};
    }

    std::map<u64, ShaderDiskCacheDecompiled> decompiled;
    std::map<ShaderDiskCacheUsage, ShaderDiskCacheDump> dumps;
    while (file.Tell() < file.GetSize()) {
        PrecompiledEntryKind kind{};
        if (file.ReadBytes(&kind, sizeof(u32)) != sizeof(u32)) {
            return {};
        }

        switch (kind) {
        case PrecompiledEntryKind::Decompiled: {
            u64 unique_identifier{};
            if (file.ReadBytes(&unique_identifier, sizeof(u64)) != sizeof(u64))
                return {};

            const auto entry = LoadDecompiledEntry(file);
            if (!entry)
                return {};
            decompiled.insert({unique_identifier, std::move(*entry)});
            break;
        }
        case PrecompiledEntryKind::Dump: {
            ShaderDiskCacheUsage usage;
            if (file.ReadBytes(&usage, sizeof(usage)) != sizeof(usage))
                return {};

            ShaderDiskCacheDump dump;
            if (file.ReadBytes(&dump.binary_format, sizeof(u32)) != sizeof(u32))
                return {};

            u32 binary_length{};
            u32 compressed_size{};
            if (file.ReadBytes(&binary_length, sizeof(u32)) != sizeof(u32) ||
                file.ReadBytes(&compressed_size, sizeof(u32)) != sizeof(u32)) {
                return {};
            }

            std::vector<u8> compressed_binary(compressed_size);
            if (file.ReadArray(compressed_binary.data(), compressed_binary.size()) !=
                compressed_binary.size()) {
                return {};
            }

            dump.binary = DecompressData(compressed_binary, binary_length);
            if (dump.binary.empty()) {
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

std::optional<ShaderDiskCacheDecompiled> ShaderDiskCacheOpenGL::LoadDecompiledEntry(
    FileUtil::IOFile& file) {
    u32 code_size{};
    u32 compressed_code_size{};
    if (file.ReadBytes(&code_size, sizeof(u32)) != sizeof(u32) ||
        file.ReadBytes(&compressed_code_size, sizeof(u32)) != sizeof(u32)) {
        return {};
    }

    std::vector<u8> compressed_code(compressed_code_size);
    if (file.ReadArray(compressed_code.data(), compressed_code.size()) != compressed_code.size()) {
        return {};
    }

    const std::vector<u8> code = DecompressData(compressed_code, code_size);
    if (code.empty()) {
        return {};
    }
    ShaderDiskCacheDecompiled entry;
    entry.code = std::string(reinterpret_cast<const char*>(code.data()), code_size);

    u32 const_buffers_count{};
    if (file.ReadBytes(&const_buffers_count, sizeof(u32)) != sizeof(u32))
        return {};
    for (u32 i = 0; i < const_buffers_count; ++i) {
        u32 max_offset{};
        u32 index{};
        u8 is_indirect{};
        if (file.ReadBytes(&max_offset, sizeof(u32)) != sizeof(u32) ||
            file.ReadBytes(&index, sizeof(u32)) != sizeof(u32) ||
            file.ReadBytes(&is_indirect, sizeof(u8)) != sizeof(u8)) {
            return {};
        }
        entry.entries.const_buffers.emplace_back(max_offset, is_indirect != 0, index);
    }

    u32 samplers_count{};
    if (file.ReadBytes(&samplers_count, sizeof(u32)) != sizeof(u32))
        return {};
    for (u32 i = 0; i < samplers_count; ++i) {
        u64 offset{};
        u64 index{};
        u32 type{};
        u8 is_array{};
        u8 is_shadow{};
        if (file.ReadBytes(&offset, sizeof(u64)) != sizeof(u64) ||
            file.ReadBytes(&index, sizeof(u64)) != sizeof(u64) ||
            file.ReadBytes(&type, sizeof(u32)) != sizeof(u32) ||
            file.ReadBytes(&is_array, sizeof(u8)) != sizeof(u8) ||
            file.ReadBytes(&is_shadow, sizeof(u8)) != sizeof(u8)) {
            return {};
        }
        entry.entries.samplers.emplace_back(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(index),
            static_cast<Tegra::Shader::TextureType>(type), is_array != 0, is_shadow != 0);
    }

    u32 global_memory_count{};
    if (file.ReadBytes(&global_memory_count, sizeof(u32)) != sizeof(u32))
        return {};
    for (u32 i = 0; i < global_memory_count; ++i) {
        u32 cbuf_index{};
        u32 cbuf_offset{};
        if (file.ReadBytes(&cbuf_index, sizeof(u32)) != sizeof(u32) ||
            file.ReadBytes(&cbuf_offset, sizeof(u32)) != sizeof(u32)) {
            return {};
        }
        entry.entries.global_memory_entries.emplace_back(cbuf_index, cbuf_offset);
    }

    for (auto& clip_distance : entry.entries.clip_distances) {
        u8 clip_distance_raw{};
        if (file.ReadBytes(&clip_distance_raw, sizeof(u8)) != sizeof(u8))
            return {};
        clip_distance = clip_distance_raw != 0;
    }

    u64 shader_length{};
    if (file.ReadBytes(&shader_length, sizeof(u64)) != sizeof(u64))
        return {};
    entry.entries.shader_length = static_cast<std::size_t>(shader_length);

    return entry;
}

bool ShaderDiskCacheOpenGL::SaveDecompiledFile(FileUtil::IOFile& file, u64 unique_identifier,
                                               const std::string& code,
                                               const std::vector<u8>& compressed_code,
                                               const GLShader::ShaderEntries& entries) {
    if (file.WriteObject(static_cast<u32>(PrecompiledEntryKind::Decompiled)) != 1 ||
        file.WriteObject(unique_identifier) != 1 ||
        file.WriteObject(static_cast<u32>(code.size())) != 1 ||
        file.WriteObject(static_cast<u32>(compressed_code.size())) != 1 ||
        file.WriteArray(compressed_code.data(), compressed_code.size()) != compressed_code.size()) {
        return false;
    }

    if (file.WriteObject(static_cast<u32>(entries.const_buffers.size())) != 1)
        return false;
    for (const auto& cbuf : entries.const_buffers) {
        if (file.WriteObject(static_cast<u32>(cbuf.GetMaxOffset())) != 1 ||
            file.WriteObject(static_cast<u32>(cbuf.GetIndex())) != 1 ||
            file.WriteObject(static_cast<u8>(cbuf.IsIndirect() ? 1 : 0)) != 1) {
            return false;
        }
    }

    if (file.WriteObject(static_cast<u32>(entries.samplers.size())) != 1)
        return false;
    for (const auto& sampler : entries.samplers) {
        if (file.WriteObject(static_cast<u64>(sampler.GetOffset())) != 1 ||
            file.WriteObject(static_cast<u64>(sampler.GetIndex())) != 1 ||
            file.WriteObject(static_cast<u32>(sampler.GetType())) != 1 ||
            file.WriteObject(static_cast<u8>(sampler.IsArray() ? 1 : 0)) != 1 ||
            file.WriteObject(static_cast<u8>(sampler.IsShadow() ? 1 : 0)) != 1) {
            return false;
        }
    }

    if (file.WriteObject(static_cast<u32>(entries.global_memory_entries.size())) != 1)
        return false;
    for (const auto& gmem : entries.global_memory_entries) {
        if (file.WriteObject(static_cast<u32>(gmem.GetCbufIndex())) != 1 ||
            file.WriteObject(static_cast<u32>(gmem.GetCbufOffset())) != 1) {
            return false;
        }
    }

    for (const bool clip_distance : entries.clip_distances) {
        if (file.WriteObject(static_cast<u8>(clip_distance ? 1 : 0)) != 1)
            return false;
    }

    return file.WriteObject(static_cast<u64>(entries.shader_length)) == 1;
}

void ShaderDiskCacheOpenGL::InvalidateTransferable() const {
    if (!FileUtil::Delete(GetTransferablePath())) {
        LOG_ERROR(Render_OpenGL, "Failed to invalidate transferable file={}",
                  GetTransferablePath());
    }
    InvalidatePrecompiled();
}

void ShaderDiskCacheOpenGL::InvalidatePrecompiled() const {
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

    const std::vector<u8> compressed_code{CompressData(code.data(), code.size())};
    if (compressed_code.empty()) {
        LOG_ERROR(Render_OpenGL, "Failed to compress GLSL code - skipping shader {:016x}",
                  unique_identifier);
        return;
    }

    FileUtil::IOFile file = AppendPrecompiledFile();
    if (!file.IsOpen())
        return;

    if (!SaveDecompiledFile(file, unique_identifier, code, compressed_code, entries)) {
        LOG_ERROR(Render_OpenGL,
                  "Failed to save decompiled entry to the precompiled file - removing");
        file.Close();
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

    const std::vector<u8> compressed_binary = CompressData(binary.data(), binary.size());
    if (compressed_binary.empty()) {
        LOG_ERROR(Render_OpenGL, "Failed to compress binary program in shader={:016x}",
                  usage.unique_identifier);
        return;
    }

    FileUtil::IOFile file = AppendPrecompiledFile();
    if (!file.IsOpen())
        return;

    if (file.WriteObject(static_cast<u32>(PrecompiledEntryKind::Dump)) != 1 ||
        file.WriteObject(usage) != 1 || file.WriteObject(static_cast<u32>(binary_format)) != 1 ||
        file.WriteObject(static_cast<u32>(binary_length)) != 1 ||
        file.WriteObject(static_cast<u32>(compressed_binary.size())) != 1 ||
        file.WriteArray(compressed_binary.data(), compressed_binary.size()) !=
            compressed_binary.size()) {
        LOG_ERROR(Render_OpenGL, "Failed to save binary program file in shader={:016x} - removing",
                  usage.unique_identifier);
        file.Close();
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

FileUtil::IOFile ShaderDiskCacheOpenGL::AppendPrecompiledFile() const {
    if (!EnsureDirectories())
        return {};

    const auto precompiled_path{GetPrecompiledPath()};
    const bool existed = FileUtil::Exists(precompiled_path);

    FileUtil::IOFile file(precompiled_path, "ab");
    if (!file.IsOpen()) {
        LOG_ERROR(Render_OpenGL, "Failed to open precompiled cache in path={}", precompiled_path);
        return {};
    }

    if (!existed || file.GetSize() == 0) {
        const auto hash{GetShaderCacheVersionHash()};
        if (file.WriteArray(hash.data(), hash.size()) != hash.size()) {
            LOG_ERROR(Render_OpenGL, "Failed to write precompiled cache version hash in path={}",
                      precompiled_path);
            return {};
        }
    }
    return file;
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