// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <fstream>
#include <locale>
#include <sstream>
#include <string_view>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/key_manager.h"
#include "core/settings.h"

namespace Core::Crypto {

Key128 GenerateKeyEncryptionKey(Key128 source, Key128 master, Key128 kek_seed, Key128 key_seed) {
    Key128 out{};

    AESCipher<Key128> cipher1(master, Mode::ECB);
    cipher1.Transcode(kek_seed.data(), kek_seed.size(), out.data(), Op::Decrypt);
    AESCipher<Key128> cipher2(out, Mode::ECB);
    cipher2.Transcode(source.data(), source.size(), out.data(), Op::Decrypt);

    if (key_seed != Key128{}) {
        AESCipher<Key128> cipher3(out, Mode::ECB);
        cipher3.Transcode(key_seed.data(), key_seed.size(), out.data(), Op::Decrypt);
    }

    return out;
}

boost::optional<Key128> DeriveSDSeed() {
    const FileUtil::IOFile save_43(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                       "/system/save/8000000000000043",
                                   "rb+");
    if (!save_43.IsOpen())
        return boost::none;
    const FileUtil::IOFile sd_private(
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) + "/Nintendo/Contents/private", "rb+");
    if (!sd_private.IsOpen())
        return boost::none;

    sd_private.Seek(0, SEEK_SET);
    std::array<u8, 0x10> private_seed{};
    if (sd_private.ReadBytes(private_seed.data(), private_seed.size()) != 0x10)
        return boost::none;

    std::array<u8, 0x10> buffer{};
    size_t offset = 0;
    for (; offset + 0x10 < save_43.GetSize(); ++offset) {
        save_43.Seek(offset, SEEK_SET);
        save_43.ReadBytes(buffer.data(), buffer.size());
        if (buffer == private_seed)
            break;
    }

    if (offset + 0x10 >= save_43.GetSize())
        return boost::none;

    Key128 seed{};
    save_43.Seek(offset + 0x10, SEEK_SET);
    save_43.ReadBytes(seed.data(), seed.size());
    return seed;
}

Loader::ResultStatus DeriveSDKeys(std::array<Key256, 2>& sd_keys, const KeyManager& keys) {
    if (!keys.HasKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::SDKEK)))
        return Loader::ResultStatus::ErrorMissingSDKEKSource;
    if (!keys.HasKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKEKGeneration)))
        return Loader::ResultStatus::ErrorMissingAESKEKGenerationSource;
    if (!keys.HasKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKeyGeneration)))
        return Loader::ResultStatus::ErrorMissingAESKeyGenerationSource;

    const auto sd_kek_source =
        keys.GetKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::SDKEK));
    const auto aes_kek_gen =
        keys.GetKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKEKGeneration));
    const auto aes_key_gen =
        keys.GetKey(S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKeyGeneration));
    const auto master_00 = keys.GetKey(S128KeyType::Master);
    const auto sd_kek =
        GenerateKeyEncryptionKey(sd_kek_source, master_00, aes_kek_gen, aes_key_gen);

    if (!keys.HasKey(S128KeyType::SDSeed))
        return Loader::ResultStatus::ErrorMissingSDSeed;
    const auto sd_seed = keys.GetKey(S128KeyType::SDSeed);

    if (!keys.HasKey(S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::Save)))
        return Loader::ResultStatus::ErrorMissingSDSaveKeySource;
    if (!keys.HasKey(S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::NCA)))
        return Loader::ResultStatus::ErrorMissingSDNCAKeySource;

    std::array<Key256, 2> sd_key_sources{
        keys.GetKey(S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::Save)),
        keys.GetKey(S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::NCA)),
    };

    // Combine sources and seed
    for (auto& source : sd_key_sources) {
        for (size_t i = 0; i < source.size(); ++i)
            source[i] ^= sd_seed[i & 0xF];
    }

    AESCipher<Key128> cipher(sd_kek, Mode::ECB);
    // The transform manipulates sd_keys as part of the Transcode, so the return/output is
    // unnecessary. This does not alter sd_keys_sources.
    std::transform(sd_key_sources.begin(), sd_key_sources.end(), sd_keys.begin(),
                   sd_key_sources.begin(), [&cipher](const Key256& source, Key256& out) {
                       cipher.Transcode(source.data(), source.size(), out.data(), Op::Decrypt);
                       return source; ///< Return unaltered source to satisfy output requirement.
                   });

    return Loader::ResultStatus::Success;
}

KeyManager::KeyManager() {
    // Initialize keys
    const std::string hactool_keys_dir = FileUtil::GetHactoolConfigurationPath();
    const std::string yuzu_keys_dir = FileUtil::GetUserPath(FileUtil::UserPath::KeysDir);
    if (Settings::values.use_dev_keys) {
        dev_mode = true;
        AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "dev.keys", false);
        AttemptLoadKeyFile(yuzu_keys_dir, yuzu_keys_dir, "dev.keys_autogenerated", false);
    } else {
        dev_mode = false;
        AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "prod.keys", false);
        AttemptLoadKeyFile(yuzu_keys_dir, yuzu_keys_dir, "prod.keys_autogenerated", false);
    }

    AttemptLoadKeyFile(yuzu_keys_dir, hactool_keys_dir, "title.keys", true);
    AttemptLoadKeyFile(yuzu_keys_dir, yuzu_keys_dir, "title.keys_autogenerated", false);
}

void KeyManager::LoadFromFile(const std::string& filename, bool is_title_keys) {
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
            auto rights_id_raw = Common::HexStringToArray<16>(out[0]);
            u128 rights_id{};
            std::memcpy(rights_id.data(), rights_id_raw.data(), rights_id_raw.size());
            Key128 key = Common::HexStringToArray<16>(out[1]);
            s128_keys[{S128KeyType::Titlekey, rights_id[1], rights_id[0]}] = key;
        } else {
            std::transform(out[0].begin(), out[0].end(), out[0].begin(), ::tolower);
            if (s128_file_id.find(out[0]) != s128_file_id.end()) {
                const auto index = s128_file_id.at(out[0]);
                Key128 key = Common::HexStringToArray<16>(out[1]);
                s128_keys[{index.type, index.field1, index.field2}] = key;
            } else if (s256_file_id.find(out[0]) != s256_file_id.end()) {
                const auto index = s256_file_id.at(out[0]);
                Key256 key = Common::HexStringToArray<32>(out[1]);
                s256_keys[{index.type, index.field1, index.field2}] = key;
            }
        }
    }
}

void KeyManager::AttemptLoadKeyFile(const std::string& dir1, const std::string& dir2,
                                    const std::string& filename, bool title) {
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

template <size_t Size>
void KeyManager::WriteKeyToFile(bool title_key, std::string_view keyname,
                                const std::array<u8, Size>& key) {
    const std::string yuzu_keys_dir = FileUtil::GetUserPath(FileUtil::UserPath::KeysDir);
    std::string filename = "title.keys_autogenerated";
    if (!title_key)
        filename = dev_mode ? "dev.keys_autogenerated" : "prod.keys_autogenerated";
    const auto add_info_text = !FileUtil::Exists(yuzu_keys_dir + DIR_SEP + filename);
    FileUtil::CreateFullPath(yuzu_keys_dir + DIR_SEP + filename);
    std::ofstream file(yuzu_keys_dir + DIR_SEP + filename, std::ios::app);
    if (!file.is_open())
        return;
    if (add_info_text) {
        file
            << "# This file is autogenerated by Yuzu\n"
            << "# It serves to store keys that were automatically generated from the normal keys\n"
            << "# If you are experiencing issues involving keys, it may help to delete this file\n";
    }

    file << std::endl
         << fmt::format("{} = {}", keyname, Common::HexArrayToString(key)) << std::endl;
    AttemptLoadKeyFile(yuzu_keys_dir, yuzu_keys_dir, filename, title_key);
}

void KeyManager::SetKey(S128KeyType id, Key128 key, u64 field1, u64 field2) {
    const auto iter = std::find_if(
        s128_file_id.begin(), s128_file_id.end(),
        [&id, &field1, &field2](const std::pair<std::string, KeyIndex<S128KeyType>> elem) {
            return std::tie(elem.second.type, elem.second.field1, elem.second.field2) ==
                   std::tie(id, field1, field2);
        });
    if (iter != s128_file_id.end())
        WriteKeyToFile(id == S128KeyType::Titlekey, iter->first, key);
    s128_keys[{id, field1, field2}] = key;
}

void KeyManager::SetKey(S256KeyType id, Key256 key, u64 field1, u64 field2) {
    const auto iter = std::find_if(
        s256_file_id.begin(), s256_file_id.end(),
        [&id, &field1, &field2](const std::pair<std::string, KeyIndex<S256KeyType>> elem) {
            return std::tie(elem.second.type, elem.second.field1, elem.second.field2) ==
                   std::tie(id, field1, field2);
        });
    if (iter != s256_file_id.end())
        WriteKeyToFile(false, iter->first, key);
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

void KeyManager::DeriveSDSeedLazy() {
    if (HasKey(S128KeyType::SDSeed))
        return;

    const auto res = DeriveSDSeed();
    if (res != boost::none)
        SetKey(S128KeyType::SDSeed, res.get());
}

const boost::container::flat_map<std::string, KeyIndex<S128KeyType>> KeyManager::s128_file_id = {
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
    {"sd_card_kek_source", {S128KeyType::Source, static_cast<u64>(SourceKeyType::SDKEK), 0}},
    {"aes_kek_generation_source",
     {S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKEKGeneration), 0}},
    {"aes_key_generation_source",
     {S128KeyType::Source, static_cast<u64>(SourceKeyType::AESKeyGeneration), 0}},
    {"sd_seed", {S128KeyType::SDSeed, 0, 0}},
};

const boost::container::flat_map<std::string, KeyIndex<S256KeyType>> KeyManager::s256_file_id = {
    {"header_key", {S256KeyType::Header, 0, 0}},
    {"sd_card_save_key_source", {S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::Save), 0}},
    {"sd_card_nca_key_source", {S256KeyType::SDKeySource, static_cast<u64>(SDKeyType::NCA), 0}},
};
} // namespace Core::Crypto
