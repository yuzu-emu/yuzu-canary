// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <random>
#include "boost/optional.hpp"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/result.h"

namespace Service::Account {
constexpr size_t MAX_USERS = 8;
constexpr size_t MAX_DATA = 128;
static const u128 INVALID_UUID = {0, 0};

struct UUID {
    // UUIDs which are 0 are considered invalid!
    u128 uuid = INVALID_UUID;
    UUID() = default;
    explicit UUID(const u128& id) : uuid{id} {}
    explicit UUID(const u64 lo, const u64 hi) {
        uuid[0] = lo;
        uuid[1] = hi;
    };
    explicit operator bool() const {
        return uuid[0] != INVALID_UUID[0] || uuid[1] != INVALID_UUID[1];
    }

    bool operator==(const UUID& rhs) const {
        return std::tie(uuid[0], uuid[1]) == std::tie(rhs.uuid[0], rhs.uuid[1]);
    }

    bool operator!=(const UUID& rhs) const {
        return !operator==(rhs);
    }

    // TODO(ogniK): Properly generate uuids based on RFC-4122
    const UUID& Generate() {
        std::random_device device;
        std::mt19937 gen(device());
        std::uniform_int_distribution<uint64_t> distribution(1,
                                                             std::numeric_limits<uint64_t>::max());
        uuid[0] = distribution(gen);
        uuid[1] = distribution(gen);
        return *this;
    }

    // Set the UUID to {0,0} to be considered an invalid user
    void Invalidate() {
        uuid = INVALID_UUID;
    }
    std::string Format() const {
        return fmt::format("0x{:016X}{:016X}", uuid[1], uuid[0]);
    }
};
static_assert(sizeof(UUID) == 16, "UUID is an invalid size!");

/// This holds general information about a users profile. This is where we store all the information
/// based on a specific user
struct ProfileInfo {
    UUID user_uuid;
    std::array<u8, 0x20> username;
    u64 creation_time;
    std::array<u8, MAX_DATA> data; // TODO(ognik): Work out what this is
    bool is_open;
};

struct ProfileBase {
    UUID user_uuid;
    u64_le timestamp;
    std::array<u8, 0x20> username;

    // Zero out all the fields to make the profile slot considered "Empty"
    void Invalidate() {
        user_uuid.Invalidate();
        timestamp = 0;
        username.fill(0);
    }
};
static_assert(sizeof(ProfileBase) == 0x38, "ProfileBase is an invalid size");

/// The profile manager is used for handling multiple user profiles at once. It keeps track of open
/// users, all the accounts registered on the "system" as well as fetching individual "ProfileInfo"
/// objects
class ProfileManager {
public:
    ProfileManager(); // TODO(ogniK): Load from system save
    ResultCode AddUser(ProfileInfo user);
    ResultCode CreateNewUser(UUID uuid, std::array<u8, 0x20>& username);
    ResultCode CreateNewUser(UUID uuid, const std::string& username);
    boost::optional<size_t> GetUserIndex(const UUID& uuid) const;
    boost::optional<size_t> GetUserIndex(ProfileInfo user) const;
    bool GetProfileBase(boost::optional<size_t> index, ProfileBase& profile) const;
    bool GetProfileBase(UUID uuid, ProfileBase& profile) const;
    bool GetProfileBase(ProfileInfo user, ProfileBase& profile) const;
    bool GetProfileBaseAndData(boost::optional<size_t> index, ProfileBase& profile,
                               std::array<u8, MAX_DATA>& data) const;
    bool GetProfileBaseAndData(UUID uuid, ProfileBase& profile,
                               std::array<u8, MAX_DATA>& data) const;
    bool GetProfileBaseAndData(ProfileInfo user, ProfileBase& profile,
                               std::array<u8, MAX_DATA>& data) const;
    size_t GetUserCount() const;
    size_t GetOpenUserCount() const;
    bool UserExists(UUID uuid) const;
    void OpenUser(UUID uuid);
    void CloseUser(UUID uuid);
    std::array<UUID, MAX_USERS> GetOpenUsers() const;
    std::array<UUID, MAX_USERS> GetAllUsers() const;
    UUID GetLastOpenedUser() const;

    bool CanSystemRegisterUser() const;

private:
    std::array<ProfileInfo, MAX_USERS> profiles{};
    size_t user_count = 0;
    boost::optional<size_t> AddToProfiles(const ProfileInfo& profile);
    bool RemoveProfileAtIndex(size_t index);
    UUID last_opened_user{0, 0};
};

}; // namespace Service::Account
