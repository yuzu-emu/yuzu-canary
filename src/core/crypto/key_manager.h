// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <type_traits>
#include <vector>
#include <boost/container/flat_map.hpp>
#include <fmt/format.h>
#include "common/common_types.h"
#include "core/loader/loader.h"

namespace Core::Crypto {

using Key128 = std::array<u8, 0x10>;
using Key256 = std::array<u8, 0x20>;
using SHA256Hash = std::array<u8, 0x20>;

static_assert(sizeof(Key128) == 16, "Key128 must be 128 bytes big.");
static_assert(sizeof(Key256) == 32, "Key128 must be 128 bytes big.");

enum class S256KeyType : u64 {
    Header,      //
    SDKeySource, // f1=SDKeyType
};

enum class S128KeyType : u64 {
    Master,        // f1=crypto revision
    Package1,      // f1=crypto revision
    Package2,      // f1=crypto revision
    Titlekek,      // f1=crypto revision
    ETicketRSAKek, //
    KeyArea,       // f1=crypto revision f2=type {app, ocean, system}
    SDSeed,        //
    Titlekey,      // f1=rights id LSB f2=rights id MSB
    Source,        // f1=source type, f2= sub id
};

enum class KeyAreaKeyType : u8 {
    Application,
    Ocean,
    System,
};

enum class SourceKeyType : u8 {
    SDKEK,
    AESKEKGeneration,
    AESKeyGeneration,
};

enum class SDKeyType : u8 {
    Save,
    NCA,
};

template <typename KeyType>
struct KeyIndex {
    KeyType type;
    u64 field1;
    u64 field2;

    std::string DebugInfo() const {
        u8 key_size = 16;
        if constexpr (std::is_same_v<KeyType, S256KeyType>)
            key_size = 32;
        return fmt::format("key_size={:02X}, key={:02X}, field1={:016X}, field2={:016X}", key_size,
                           static_cast<u8>(type), field1, field2);
    }
};

// boost flat_map requires operator< for O(log(n)) lookups.
template <typename KeyType>
bool operator<(const KeyIndex<KeyType>& lhs, const KeyIndex<KeyType>& rhs) {
    return std::tie(lhs.type, lhs.field1, lhs.field2) < std::tie(rhs.type, rhs.field1, rhs.field2);
}

class KeyManager {
public:
    KeyManager();

    bool HasKey(S128KeyType id, u64 field1 = 0, u64 field2 = 0) const;
    bool HasKey(S256KeyType id, u64 field1 = 0, u64 field2 = 0) const;

    Key128 GetKey(S128KeyType id, u64 field1 = 0, u64 field2 = 0) const;
    Key256 GetKey(S256KeyType id, u64 field1 = 0, u64 field2 = 0) const;

    void SetKey(S128KeyType id, Key128 key, u64 field1 = 0, u64 field2 = 0);
    void SetKey(S256KeyType id, Key256 key, u64 field1 = 0, u64 field2 = 0);

    static bool KeyFileExists(bool title);

    // Call before using the sd seed to attempt to derive it if it dosen't exist. Needs system save
    // 8*43 and the private file to exist.
    void DeriveSDSeedLazy();

private:
    boost::container::flat_map<KeyIndex<S128KeyType>, Key128> s128_keys;
    boost::container::flat_map<KeyIndex<S256KeyType>, Key256> s256_keys;

    bool dev_mode;
    void LoadFromFile(const std::string& filename, bool is_title_keys);
    void AttemptLoadKeyFile(const std::string& dir1, const std::string& dir2,
                            const std::string& filename, bool title);
    template <size_t Size>
    void WriteKeyToFile(bool title_key, std::string_view keyname, const std::array<u8, Size>& key);

    static const boost::container::flat_map<std::string, KeyIndex<S128KeyType>> s128_file_id;
    static const boost::container::flat_map<std::string, KeyIndex<S256KeyType>> s256_file_id;
};

Key128 GenerateKeyEncryptionKey(Key128 source, Key128 master, Key128 kek_seed, Key128 key_seed);
boost::optional<Key128> DeriveSDSeed();
Loader::ResultStatus DeriveSDKeys(std::array<Key256, 2>& sd_keys, const KeyManager& keys);

} // namespace Core::Crypto
