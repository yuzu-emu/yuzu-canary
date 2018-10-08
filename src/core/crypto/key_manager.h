// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <string>
#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <fmt/format.h>
#include "common/common_types.h"
#include "core/crypto/partition_data_manager.h"
#include "core/file_sys/vfs_types.h"

namespace FileUtil {
class IOFile;
}

namespace Loader {
enum class ResultStatus : u16;
}

namespace Core::Crypto {

constexpr u64 TICKET_FILE_TITLEKEY_OFFSET = 0x180;

using Key128 = std::array<u8, 0x10>;
using Key256 = std::array<u8, 0x20>;
using SHA256Hash = std::array<u8, 0x20>;
using TicketRaw = std::array<u8, 0x400>;

static_assert(sizeof(Key128) == 16, "Key128 must be 128 bytes big.");
static_assert(sizeof(Key256) == 32, "Key256 must be 256 bytes big.");

template <size_t bit_size, size_t byte_size = (bit_size >> 3)>
struct RSAKeyPair {
    std::array<u8, byte_size> encryption_key;
    std::array<u8, byte_size> decryption_key;
    std::array<u8, byte_size> modulus;
    std::array<u8, 4> exponent;
};

enum class KeyCategory : u8 {
    Standard,
    Title,
    Console,
};

enum class S256KeyType : u64 {
    SDKey,        // f1=SDKeyType
    Header,       //
    SDKeySource,  // f1=SDKeyType
    HeaderSource, //
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
    Keyblob,       // f1=crypto revision
    KeyblobMAC,    // f1=crypto revision
    TSEC,          //
    SecureBoot,    //
    BIS,           // f1=partition (0-3), f2=type {crypt, tweak}
    HeaderKek,     //
    SDKek,         //
    RSAKek,        //
};

enum class KeyAreaKeyType : u8 {
    Application,
    Ocean,
    System,
};

enum class SourceKeyType : u8 {
    SDKek,                //
    AESKekGeneration,     //
    AESKeyGeneration,     //
    RSAOaepKekGeneration, //
    Master,               //
    Keyblob,              // f2=crypto revision
    KeyAreaKey,           // f2=KeyAreaKeyType
    Titlekek,             //
    Package2,             //
    HeaderKek,            //
    KeyblobMAC,           //
    ETicketKek,           //
    ETicketKekek,         //
};

enum class SDKeyType : u8 {
    Save,
    NCA,
};

enum class BISKeyType : u8 {
    Crypto,
    Tweak,
};

enum class RSAKekType : u8 {
    Mask0,
    Seed3,
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

    Key256 GetBISKey(u8 partition_id) const;

    void SetKey(S128KeyType id, Key128 key, u64 field1 = 0, u64 field2 = 0);
    void SetKey(S256KeyType id, Key256 key, u64 field1 = 0, u64 field2 = 0);

    static bool KeyFileExists(bool title);

    // Call before using the sd seed to attempt to derive it if it dosen't exist. Needs system save
    // 8*43 and the private file to exist.
    void DeriveSDSeedLazy();

    bool BaseDeriveNecessary() const;
    void DeriveBase();
    void DeriveETicket(PartitionDataManager& data);

    void PopulateFromPartitionData(PartitionDataManager& data);

private:
    std::map<KeyIndex<S128KeyType>, Key128> s128_keys;
    std::map<KeyIndex<S256KeyType>, Key256> s256_keys;

    std::array<std::array<u8, 0xB0>, 0x20> encrypted_keyblobs{};
    std::array<std::array<u8, 0x90>, 0x20> keyblobs{};

    bool dev_mode;
    void LoadFromFile(const std::string& filename, bool is_title_keys);
    void AttemptLoadKeyFile(const std::string& dir1, const std::string& dir2,
                            const std::string& filename, bool title);
    template <size_t Size>
    void WriteKeyToFile(KeyCategory category, std::string_view keyname,
                        const std::array<u8, Size>& key);

    void DeriveGeneralPurposeKeys(u8 crypto_revision);

    void SetKeyWrapped(S128KeyType id, Key128 key, u64 field1 = 0, u64 field2 = 0);
    void SetKeyWrapped(S256KeyType id, Key256 key, u64 field1 = 0, u64 field2 = 0);

    static const boost::container::flat_map<std::string, KeyIndex<S128KeyType>> s128_file_id;
    static const boost::container::flat_map<std::string, KeyIndex<S256KeyType>> s256_file_id;
};

Key128 GenerateKeyEncryptionKey(Key128 source, Key128 master, Key128 kek_seed, Key128 key_seed);
Key128 DeriveKeyblobKey(const Key128& sbk, const Key128& tsec, Key128 source);
Key128 DeriveKeyblobMACKey(const Key128& keyblob_key, const Key128& mac_source);
Key128 DeriveMasterKey(const std::array<u8, 0x90>& keyblob, const Key128& master_source);
std::array<u8, 0x90> DecryptKeyblob(const std::array<u8, 0xB0>& encrypted_keyblob,
                                    const Key128& key);

boost::optional<Key128> DeriveSDSeed();
Loader::ResultStatus DeriveSDKeys(std::array<Key256, 2>& sd_keys, KeyManager& keys);

std::vector<TicketRaw> GetTicketblob(const FileUtil::IOFile& ticket_save);

// Returns a pair of {rights_id, titlekey}. Fails if the ticket has no certificate authority (offset
// 0x140-0x144 is zero)
boost::optional<std::pair<Key128, Key128>> ParseTicket(
    const TicketRaw& ticket, const RSAKeyPair<2048>& eticket_extended_key);

} // namespace Core::Crypto
