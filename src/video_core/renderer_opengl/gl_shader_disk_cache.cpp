// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
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

constexpr u32 NativeVersion = 4;

// Making sure sizes doesn't change by accident
static_assert(sizeof(BaseBindings) == 16);
static_assert(sizeof(ShaderDiskCacheUsage) == 40);

namespace {

ShaderCacheVersionHash GetShaderCacheVersionHash() {
    ShaderCacheVersionHash hash{};
    const std::size_t length = std::min(std::strlen(Common::g_shader_cache_version), hash.size());
    std::memcpy(hash.data(), Common::g_shader_cache_version, length);
    return hash;
}

} // namespace

ShaderDiskCacheRaw::ShaderDiskCacheRaw(u64 unique_identifier, ProgramType program_type,
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

ShaderDiskCacheOpenGL::~ShaderDiskCacheOpenGL() = default;

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

std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>, ShaderDumpsMap>
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

std::optional<std::pair<std::unordered_map<u64, ShaderDiskCacheDecompiled>, ShaderDumpsMap>>
ShaderDiskCacheOpenGL::LoadPrecompiledFile(FileUtil::IOFile& file) {
    // Read compressed file from disk and decompress to virtual precompiled cache file
    std::vector<u8> compressed(file.GetSize());
    file.ReadBytes(compressed.data(), compressed.size());
    const std::vector<u8> decompressed = Common::Compression::DecompressDataZSTD(compressed);
    SaveArrayToPrecompiled(decompressed.data(), decompressed.size());
    precompiled_cache_virtual_file_offset = 0;

    ShaderCacheVersionHash file_hash{};
    if (!LoadArrayFromPrecompiled(file_hash.data(), file_hash.size())) {
        precompiled_cache_virtual_file_offset = 0;
        return {};
    }
    if (GetShaderCacheVersionHash() != file_hash) {
        LOG_INFO(Render_OpenGL, "Precompiled cache is from another version of the emulator");
        precompiled_cache_virtual_file_offset = 0;
        return {};
    }

    std::unordered_map<u64, ShaderDiskCacheDecompiled> decompiled;
    ShaderDumpsMap dumps;
    while (precompiled_cache_virtual_file_offset < precompiled_cache_virtual_file.GetSize()) {
        PrecompiledEntryKind kind{};
        if (!LoadObjectFromPrecompiled(kind)) {
            return {};
        }

        switch (kind) {
        case PrecompiledEntryKind::Decompiled: {
            u64 unique_identifier{};
            if (!LoadObjectFromPrecompiled(unique_identifier)) {
                return {};
            }

            auto entry = LoadDecompiledEntry();
            if (!entry) {
                return {};
            }
            decompiled.insert({unique_identifier, std::move(*entry)});
            break;
        }
        case PrecompiledEntryKind::Dump: {
            ShaderDiskCacheUsage usage;
            if (!LoadObjectFromPrecompiled(usage)) {
                return {};
            }

            ShaderDiskCacheDump dump;
            if (!LoadObjectFromPrecompiled(dump.binary_format)) {
                return {};
            }

            u32 binary_length{};
            if (!LoadObjectFromPrecompiled(binary_length)) {
                return {};
            }

            dump.binary.resize(binary_length);
            if (!LoadArrayFromPrecompiled(dump.binary.data(), dump.binary.size())) {
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
    if (!LoadObjectFromPrecompiled(code_size)) {
        return {};
    }

    std::string code(code_size, '\0');
    if (!LoadArrayFromPrecompiled(code.data(), code.size())) {
        return {};
    }

    ShaderDiskCacheDecompiled entry;
    entry.code = std::move(code);

    u32 const_buffers_count{};
    if (!LoadObjectFromPrecompiled(const_buffers_count)) {
        return {};
    }

    for (u32 i = 0; i < const_buffers_count; ++i) {
        u32 max_offset{};
        u32 index{};
        bool is_indirect{};
        if (!LoadObjectFromPrecompiled(max_offset) || !LoadObjectFromPrecompiled(index) ||
            !LoadObjectFromPrecompiled(is_indirect)) {
            return {};
        }
        entry.entries.const_buffers.emplace_back(max_offset, is_indirect, index);
    }

    u32 samplers_count{};
    if (!LoadObjectFromPrecompiled(samplers_count)) {
        return {};
    }

    for (u32 i = 0; i < samplers_count; ++i) {
        u64 offset{};
        u64 index{};
        u32 type{};
        bool is_array{};
        bool is_shadow{};
        bool is_bindless{};
        if (!LoadObjectFromPrecompiled(offset) || !LoadObjectFromPrecompiled(index) ||
            !LoadObjectFromPrecompiled(type) || !LoadObjectFromPrecompiled(is_array) ||
            !LoadObjectFromPrecompiled(is_shadow) || !LoadObjectFromPrecompiled(is_bindless)) {
            return {};
        }
        entry.entries.samplers.emplace_back(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(index),
            static_cast<Tegra::Shader::TextureType>(type), is_array, is_shadow, is_bindless);
    }

    u32 images_count{};
    if (!LoadObjectFromPrecompiled(images_count)) {
        return {};
    }
    for (u32 i = 0; i < images_count; ++i) {
        u64 offset{};
        u64 index{};
        u32 type{};
        u8 is_bindless{};
        u8 is_written{};
        u8 is_read{};
        u8 is_atomic{};
        if (!LoadObjectFromPrecompiled(offset) || !LoadObjectFromPrecompiled(index) ||
            !LoadObjectFromPrecompiled(type) || !LoadObjectFromPrecompiled(is_bindless) ||
            !LoadObjectFromPrecompiled(is_written) || !LoadObjectFromPrecompiled(is_read) ||
            !LoadObjectFromPrecompiled(is_atomic)) {
            return {};
        }
        entry.entries.images.emplace_back(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(index),
            static_cast<Tegra::Shader::ImageType>(type), is_bindless != 0, is_written != 0,
            is_read != 0, is_atomic != 0);
    }

    u32 global_memory_count{};
    if (!LoadObjectFromPrecompiled(global_memory_count)) {
        return {};
    }
    for (u32 i = 0; i < global_memory_count; ++i) {
        u32 cbuf_index{};
        u32 cbuf_offset{};
        bool is_read{};
        bool is_written{};
        if (!LoadObjectFromPrecompiled(cbuf_index) || !LoadObjectFromPrecompiled(cbuf_offset) ||
            !LoadObjectFromPrecompiled(is_read) || !LoadObjectFromPrecompiled(is_written)) {
            return {};
        }
        entry.entries.global_memory_entries.emplace_back(cbuf_index, cbuf_offset, is_read,
                                                         is_written);
    }

    for (auto& clip_distance : entry.entries.clip_distances) {
        if (!LoadObjectFromPrecompiled(clip_distance)) {
            return {};
        }
    }

    u64 shader_length{};
    if (!LoadObjectFromPrecompiled(shader_length)) {
        return {};
    }
    entry.entries.shader_length = static_cast<std::size_t>(shader_length);

    return entry;
}

bool ShaderDiskCacheOpenGL::SaveDecompiledFile(u64 unique_identifier, const std::string& code,
                                               const GLShader::ShaderEntries& entries) {
    if (!SaveObjectToPrecompiled(static_cast<u32>(PrecompiledEntryKind::Decompiled)) ||
        !SaveObjectToPrecompiled(unique_identifier) ||
        !SaveObjectToPrecompiled(static_cast<u32>(code.size())) ||
        !SaveArrayToPrecompiled(code.data(), code.size())) {
        return false;
    }

    if (!SaveObjectToPrecompiled(static_cast<u32>(entries.const_buffers.size()))) {
        return false;
    }
    for (const auto& cbuf : entries.const_buffers) {
        if (!SaveObjectToPrecompiled(static_cast<u32>(cbuf.GetMaxOffset())) ||
            !SaveObjectToPrecompiled(static_cast<u32>(cbuf.GetIndex())) ||
            !SaveObjectToPrecompiled(cbuf.IsIndirect())) {
            return false;
        }
    }

    if (!SaveObjectToPrecompiled(static_cast<u32>(entries.samplers.size()))) {
        return false;
    }
    for (const auto& sampler : entries.samplers) {
        if (!SaveObjectToPrecompiled(static_cast<u64>(sampler.GetOffset())) ||
            !SaveObjectToPrecompiled(static_cast<u64>(sampler.GetIndex())) ||
            !SaveObjectToPrecompiled(static_cast<u32>(sampler.GetType())) ||
            !SaveObjectToPrecompiled(sampler.IsArray()) ||
            !SaveObjectToPrecompiled(sampler.IsShadow()) ||
            !SaveObjectToPrecompiled(sampler.IsBindless())) {
            return false;
        }
    }

    if (!SaveObjectToPrecompiled(static_cast<u32>(entries.images.size()))) {
        return false;
    }
    for (const auto& image : entries.images) {
        if (!SaveObjectToPrecompiled(static_cast<u64>(image.GetOffset())) ||
            !SaveObjectToPrecompiled(static_cast<u64>(image.GetIndex())) ||
            !SaveObjectToPrecompiled(static_cast<u32>(image.GetType())) ||
            !SaveObjectToPrecompiled(static_cast<u8>(image.IsBindless() ? 1 : 0)) ||
            !SaveObjectToPrecompiled(static_cast<u8>(image.IsWritten() ? 1 : 0)) ||
            !SaveObjectToPrecompiled(static_cast<u8>(image.IsRead() ? 1 : 0)) ||
            !SaveObjectToPrecompiled(static_cast<u8>(image.IsAtomic() ? 1 : 0))) {
            return false;
        }
    }

    if (!SaveObjectToPrecompiled(static_cast<u32>(entries.global_memory_entries.size()))) {
        return false;
    }
    for (const auto& gmem : entries.global_memory_entries) {
        if (!SaveObjectToPrecompiled(static_cast<u32>(gmem.GetCbufIndex())) ||
            !SaveObjectToPrecompiled(static_cast<u32>(gmem.GetCbufOffset())) ||
            !SaveObjectToPrecompiled(gmem.IsRead()) || !SaveObjectToPrecompiled(gmem.IsWritten())) {
            return false;
        }
    }

    for (const bool clip_distance : entries.clip_distances) {
        if (!SaveObjectToPrecompiled(clip_distance)) {
            return false;
        }
    }

    if (!SaveObjectToPrecompiled(static_cast<u64>(entries.shader_length))) {
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
    precompiled_cache_virtual_file.Resize(0);

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
    if (usages.find(usage) != usages.end()) {
        // Skip this variant since the shader is already stored.
        return;
    }
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

    if (precompiled_cache_virtual_file.GetSize() == 0) {
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

    if (!SaveObjectToPrecompiled(static_cast<u32>(PrecompiledEntryKind::Dump)) ||
        !SaveObjectToPrecompiled(usage) ||
        !SaveObjectToPrecompiled(static_cast<u32>(binary_format)) ||
        !SaveObjectToPrecompiled(static_cast<u32>(binary_length)) ||
        !SaveArrayToPrecompiled(binary.data(), binary.size())) {
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
    if (!SaveArrayToPrecompiled(hash.data(), hash.size())) {
        LOG_ERROR(
            Render_OpenGL,
            "Failed to write precompiled cache version hash to virtual precompiled cache file");
    }
}

void ShaderDiskCacheOpenGL::SaveVirtualPrecompiledFile() {
    precompiled_cache_virtual_file_offset = 0;
    const std::vector<u8>& uncompressed = precompiled_cache_virtual_file.ReadAllBytes();
    const std::vector<u8>& compressed =
        Common::Compression::CompressDataZSTDDefault(uncompressed.data(), uncompressed.size());

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
