// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <fstream>
#include <locale>
#include <sstream>
#include <string_view>
#include <mbedtls/sha256.h>
#include "common/assert.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/crypto/key_manager.h"
#include "core/settings.h"
#include "key_manager.h"

namespace Core::Crypto {

static u8 ToHexNibble(char c1) {
    if (c1 >= 65 && c1 <= 70)
        return c1 - 55;
    if (c1 >= 97 && c1 <= 102)
        return c1 - 87;
    if (c1 >= 48 && c1 <= 57)
        return c1 - 48;
    throw std::logic_error("Invalid hex digit");
}

template <size_t Size>
static std::array<u8, Size> HexStringToArray(std::string_view str) {
    std::array<u8, Size> out{};
    for (size_t i = 0; i < 2 * Size; i += 2) {
        auto d1 = str[i];
        auto d2 = str[i + 1];
        out[i / 2] = (ToHexNibble(d1) << 4) | ToHexNibble(d2);
    }
    return out;
}

std::array<u8, 16> operator""_array16(const char* str, size_t len) {
    if (len != 32)
        throw std::logic_error("Not of correct size.");
    return HexStringToArray<16>(str);
}

std::array<u8, 32> operator""_array32(const char* str, size_t len) {
    if (len != 64)
        throw std::logic_error("Not of correct size.");
    return HexStringToArray<32>(str);
}

KeyManager::KeyManager() {
    // Initialize keys
    const std::string hactool_keys_dir = FileUtil::GetHactoolConfigurationPath();
    const std::string yuzu_keys_dir = FileUtil::GetUserPath(FileUtil::UserPath::KeysDir);
    if (Settings::values.use_dev_keys) {
        dev_mode = true;
        AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "dev.keys", false);
    } else {
        dev_mode = false;
        AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "prod.keys", false);
    }

    AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "title.keys", true);
}

void KeyManager::LoadFromFile(std::string_view filename_, bool is_title_keys) {
    const auto filename = std::string(filename_);
    std::ifstream file(filename);
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line)) {
        std::vector<std::string> out;
        std::stringstream stream(line);
        std::string item;
        while (std::getline(stream, item, '='))
            out.push_back(std::move(item));

        if (out.size() != 2)
            continue;

        out[0].erase(std::remove(out[0].begin(), out[0].end(), ' '), out[0].end());
        out[1].erase(std::remove(out[1].begin(), out[1].end(), ' '), out[1].end());

        if (is_title_keys) {
            auto rights_id_raw = HexStringToArray<16>(out[0]);
            u128 rights_id{};
            std::memcpy(rights_id.data(), rights_id_raw.data(), rights_id_raw.size());
            Key128 key = HexStringToArray<16>(out[1]);
            SetKey(S128KeyType::Titlekey, key, rights_id[1], rights_id[0]);
        } else {
            std::transform(out[0].begin(), out[0].end(), out[0].begin(), ::tolower);
            if (s128_file_id.find(out[0]) != s128_file_id.end()) {
                const auto index = s128_file_id.at(out[0]);
                Key128 key = HexStringToArray<16>(out[1]);
                SetKey(index.type, key, index.field1, index.field2);
            } else if (s256_file_id.find(out[0]) != s256_file_id.end()) {
                const auto index = s256_file_id.at(out[0]);
                Key256 key = HexStringToArray<32>(out[1]);
                SetKey(index.type, key, index.field1, index.field2);
            }
        }
    }
}

void KeyManager::AttemptLoadKeyFile(std::string_view dir1_, std::string_view dir2_,
                                    std::string_view filename_, bool title) {
    const std::string dir1(dir1_);
    const std::string dir2(dir2_);
    const std::string filename(filename_);
    if (FileUtil::Exists(dir1 + DIR_SEP + filename))
        LoadFromFile(dir1 + DIR_SEP + filename, title);
    else if (FileUtil::Exists(dir2 + DIR_SEP + filename))
        LoadFromFile(dir2 + DIR_SEP + filename, title);
}

bool KeyManager::HasKey(S128KeyType id, u64 field1, u64 field2) const {
    return s128_keys.find({id, field1, field2}) != s128_keys.end();
}

bool KeyManager::HasKey(S256KeyType id, u64 field1, u64 field2) const {
    return s256_keys.find({id, field1, field2}) != s256_keys.end();
}

Key128 KeyManager::GetKey(S128KeyType id, u64 field1, u64 field2) const {
    if (!HasKey(id, field1, field2))
        return {};
    return s128_keys.at({id, field1, field2});
}

Key256 KeyManager::GetKey(S256KeyType id, u64 field1, u64 field2) const {
    if (!HasKey(id, field1, field2))
        return {};
    return s256_keys.at({id, field1, field2});
}

void KeyManager::SetKey(S128KeyType id, Key128 key, u64 field1, u64 field2) {
    s128_keys[{id, field1, field2}] = key;
}

void KeyManager::SetKey(S256KeyType id, Key256 key, u64 field1, u64 field2) {
    s256_keys[{id, field1, field2}] = key;
}

bool KeyManager::KeyFileExists(bool title) {
    const std::string hactool_keys_dir = FileUtil::GetHactoolConfigurationPath();
    const std::string yuzu_keys_dir = FileUtil::GetUserPath(FileUtil::UserPath::KeysDir);
    if (title) {
        return FileUtil::Exists(hactool_keys_dir + DIR_SEP + "title.keys") ||
               FileUtil::Exists(yuzu_keys_dir + DIR_SEP + "title.keys");
    }

    if (Settings::values.use_dev_keys) {
        return FileUtil::Exists(hactool_keys_dir + DIR_SEP + "dev.keys") ||
               FileUtil::Exists(yuzu_keys_dir + DIR_SEP + "dev.keys");
    }

    return FileUtil::Exists(hactool_keys_dir + DIR_SEP + "prod.keys") ||
           FileUtil::Exists(yuzu_keys_dir + DIR_SEP + "prod.keys");
}

const std::unordered_map<std::string, KeyIndex<S128KeyType>> KeyManager::s128_file_id = {
    {"master_key_00", {S128KeyType::Master, 0, 0}},
    {"master_key_01", {S128KeyType::Master, 1, 0}},
    {"master_key_02", {S128KeyType::Master, 2, 0}},
    {"master_key_03", {S128KeyType::Master, 3, 0}},
    {"master_key_04", {S128KeyType::Master, 4, 0}},
    {"package1_key_00", {S128KeyType::Package1, 0, 0}},
    {"package1_key_01", {S128KeyType::Package1, 1, 0}},
    {"package1_key_02", {S128KeyType::Package1, 2, 0}},
    {"package1_key_03", {S128KeyType::Package1, 3, 0}},
    {"package1_key_04", {S128KeyType::Package1, 4, 0}},
    {"package2_key_00", {S128KeyType::Package2, 0, 0}},
    {"package2_key_01", {S128KeyType::Package2, 1, 0}},
    {"package2_key_02", {S128KeyType::Package2, 2, 0}},
    {"package2_key_03", {S128KeyType::Package2, 3, 0}},
    {"package2_key_04", {S128KeyType::Package2, 4, 0}},
    {"titlekek_00", {S128KeyType::Titlekek, 0, 0}},
    {"titlekek_01", {S128KeyType::Titlekek, 1, 0}},
    {"titlekek_02", {S128KeyType::Titlekek, 2, 0}},
    {"titlekek_03", {S128KeyType::Titlekek, 3, 0}},
    {"titlekek_04", {S128KeyType::Titlekek, 4, 0}},
    {"eticket_rsa_kek", {S128KeyType::ETicketRSAKek, 0, 0}},
    {"key_area_key_application_00",
     {S128KeyType::KeyArea, 0, static_cast<u64>(KeyAreaKeyType::Application)}},
    {"key_area_key_application_01",
     {S128KeyType::KeyArea, 1, static_cast<u64>(KeyAreaKeyType::Application)}},
    {"key_area_key_application_02",
     {S128KeyType::KeyArea, 2, static_cast<u64>(KeyAreaKeyType::Application)}},
    {"key_area_key_application_03",
     {S128KeyType::KeyArea, 3, static_cast<u64>(KeyAreaKeyType::Application)}},
    {"key_area_key_application_04",
     {S128KeyType::KeyArea, 4, static_cast<u64>(KeyAreaKeyType::Application)}},
    {"key_area_key_ocean_00", {S128KeyType::KeyArea, 0, static_cast<u64>(KeyAreaKeyType::Ocean)}},
    {"key_area_key_ocean_01", {S128KeyType::KeyArea, 1, static_cast<u64>(KeyAreaKeyType::Ocean)}},
    {"key_area_key_ocean_02", {S128KeyType::KeyArea, 2, static_cast<u64>(KeyAreaKeyType::Ocean)}},
    {"key_area_key_ocean_03", {S128KeyType::KeyArea, 3, static_cast<u64>(KeyAreaKeyType::Ocean)}},
    {"key_area_key_ocean_04", {S128KeyType::KeyArea, 4, static_cast<u64>(KeyAreaKeyType::Ocean)}},
    {"key_area_key_system_00", {S128KeyType::KeyArea, 0, static_cast<u64>(KeyAreaKeyType::System)}},
    {"key_area_key_system_01", {S128KeyType::KeyArea, 1, static_cast<u64>(KeyAreaKeyType::System)}},
    {"key_area_key_system_02", {S128KeyType::KeyArea, 2, static_cast<u64>(KeyAreaKeyType::System)}},
    {"key_area_key_system_03", {S128KeyType::KeyArea, 3, static_cast<u64>(KeyAreaKeyType::System)}},
    {"key_area_key_system_04", {S128KeyType::KeyArea, 4, static_cast<u64>(KeyAreaKeyType::System)}},
};

const std::unordered_map<std::string, KeyIndex<S256KeyType>> KeyManager::s256_file_id = {
    {"header_key", {S256KeyType::Header, 0, 0}},
    {"sd_card_save_key", {S256KeyType::SDSave, 0, 0}},
    {"sd_card_nca_key", {S256KeyType::SDNCA, 0, 0}},
};
} // namespace Core::Crypto
