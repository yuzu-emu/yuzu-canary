// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <fstream>
#include <fmt/format.h>
#include <json.hpp>

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "video_core/texture_cache/resolution_scaling/database.h"

namespace VideoCommon::Resolution {

using namespace nlohmann;

std::string GetBaseDir() {
    return FileUtil::GetUserPath(FileUtil::UserPath::RescalingDir);
}

ScalingDatabase::ScalingDatabase(Core::System& system) : system{system} {}

ScalingDatabase::~ScalingDatabase() {
    SaveDatabase();
}

void ScalingDatabase::Init() {
    title_id = system.CurrentProcess()->GetTitleID();
    LoadDatabase();
    initialized = true;
}

void ScalingDatabase::LoadDatabase() {
    const std::string path = GetProfilePath();
    const bool exists = FileUtil::Exists(path);
    if (!exists) {
        return;
    }
    std::ifstream file;
    OpenFStream(file, path, std::ios_base::in);
    json in;
    file >> in;
    u32 version = in["version"].get<u32>();
    if (version != DBVersion) {
        return;
    }
    for (const auto& entry : in["entries"]) {
        ResolutionKey key{};
        key.format = static_cast<PixelFormat>(entry["format"].get<u32>());
        key.width = entry["width"].get<u32>();
        key.height = entry["height"].get<u32>();
        database.insert(key);
    }
    for (const auto& entry : in["blacklist"]) {
        ResolutionKey key{};
        key.format = static_cast<PixelFormat>(entry["format"].get<u32>());
        key.width = entry["width"].get<u32>();
        key.height = entry["height"].get<u32>();
        blacklist.insert(key);
    }
}

void ScalingDatabase::SaveDatabase() {
    const std::string dir = GetBaseDir();
    if (!FileUtil::CreateDir(dir)) {
        LOG_ERROR(HW_GPU, "Failed to create directory={}", dir);
        return;
    }
    json out;
    out.emplace("version", DBVersion);
    auto entries = json::array();
    for (const auto& key : database) {
        entries.push_back({
            {"format", static_cast<u32>(key.format)},
            {"width", key.width},
            {"height", key.height},
        });
    }
    out.emplace("entries", std::move(entries));
    auto blacklist_entries = json::array();
    for (const auto& key : blacklist) {
        blacklist_entries.push_back({
            {"format", static_cast<u32>(key.format)},
            {"width", key.width},
            {"height", key.height},
        });
    }
    out.emplace("blacklist", std::move(blacklist_entries));
    const std::string path = GetProfilePath();
    std::ofstream file;
    OpenFStream(file, path, std::ios_base::out);
    file << std::setw(4) << out << std::endl;
}

void ScalingDatabase::Register(PixelFormat format, u32 width, u32 height) {
    const ResolutionKey key{format, width, height};
    if (blacklist.count(key) == 0) {
        database.insert(key);
    }
}

void ScalingDatabase::Unregister(PixelFormat format, u32 width, u32 height) {
    const ResolutionKey key{format, width, height};
    database.erase(key);
    blacklist.insert(key);
}

std::string ScalingDatabase::GetTitleID() const {
    return fmt::format("{:016X}", title_id);
}

std::string ScalingDatabase::GetProfilePath() const {
    return FileUtil::SanitizePath(GetBaseDir() + DIR_SEP_CHR + GetTitleID() + ".json");
}

} // namespace VideoCommon::Resolution
