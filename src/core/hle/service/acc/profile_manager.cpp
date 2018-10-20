// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <random>
#include <boost/optional.hpp>
#include "common/file_util.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/settings.h"

namespace Service::Account {

struct UserRaw {
    UUID uuid;
    UUID uuid2;
    u64 timestamp;
    ProfileUsername username;
    INSERT_PADDING_BYTES(0x80);
};
static_assert(sizeof(UserRaw) == 0xC8, "UserRaw has incorrect size.");

struct ProfileDataRaw {
    INSERT_PADDING_BYTES(0x10);
    std::array<UserRaw, MAX_USERS> users;
};
static_assert(sizeof(ProfileDataRaw) == 0x650, "ProfileDataRaw has incorrect size.");

// TODO(ogniK): Get actual error codes
constexpr ResultCode ERROR_TOO_MANY_USERS(ErrorModule::Account, -1);
constexpr ResultCode ERROR_USER_ALREADY_EXISTS(ErrorModule::Account, -2);
constexpr ResultCode ERROR_ARGUMENT_IS_NULL(ErrorModule::Account, 20);

constexpr char ACC_SAVE_AVATORS_BASE_PATH[] = "/system/save/8000000000000010/su/avators/";

UUID UUID::Generate() {
    std::random_device device;
    std::mt19937 gen(device());
    std::uniform_int_distribution<u64> distribution(1, std::numeric_limits<u64>::max());
    return UUID{distribution(gen), distribution(gen)};
}

ProfileManager::ProfileManager() {
    ParseUserSaveFile();

    if (user_count == 0)
        CreateNewUser(UUID::Generate(), "yuzu");

    auto current = std::clamp<int>(Settings::values.current_user, 0, MAX_USERS - 1);
    if (UserExistsIndex(current))
        current = 0;

    OpenUser(*GetUser(current));
}

ProfileManager::~ProfileManager() {
    WriteUserSaveFile();
}

/// After a users creation it needs to be "registered" to the system. AddToProfiles handles the
/// internal management of the users profiles
boost::optional<std::size_t> ProfileManager::AddToProfiles(const ProfileInfo& user) {
    if (user_count >= MAX_USERS) {
        return boost::none;
    }
    profiles[user_count] = user;
    return user_count++;
}

/// Deletes a specific profile based on it's profile index
bool ProfileManager::RemoveProfileAtIndex(std::size_t index) {
    if (index >= MAX_USERS || index >= user_count) {
        return false;
    }
    if (index < user_count - 1) {
        std::rotate(profiles.begin() + index, profiles.begin() + index + 1, profiles.end());
    }
    profiles.back() = {};
    user_count--;
    return true;
}

/// Helper function to register a user to the system
ResultCode ProfileManager::AddUser(const ProfileInfo& user) {
    if (AddToProfiles(user) == boost::none) {
        return ERROR_TOO_MANY_USERS;
    }
    return RESULT_SUCCESS;
}

/// Create a new user on the system. If the uuid of the user already exists, the user is not
/// created.
ResultCode ProfileManager::CreateNewUser(UUID uuid, const ProfileUsername& username) {
    if (user_count == MAX_USERS) {
        return ERROR_TOO_MANY_USERS;
    }
    if (!uuid) {
        return ERROR_ARGUMENT_IS_NULL;
    }
    if (username[0] == 0x0) {
        return ERROR_ARGUMENT_IS_NULL;
    }
    if (std::any_of(profiles.begin(), profiles.end(),
                    [&uuid](const ProfileInfo& profile) { return uuid == profile.user_uuid; })) {
        return ERROR_USER_ALREADY_EXISTS;
    }
    ProfileInfo profile;
    profile.user_uuid = uuid;
    profile.username = username;
    profile.data = {};
    profile.creation_time = 0x0;
    profile.is_open = false;
    return AddUser(profile);
}

/// Creates a new user on the system. This function allows a much simpler method of registration
/// specifically by allowing an std::string for the username. This is required specifically since
/// we're loading a string straight from the config
ResultCode ProfileManager::CreateNewUser(UUID uuid, const std::string& username) {
    ProfileUsername username_output{};

    if (username.size() > username_output.size()) {
        std::copy_n(username.begin(), username_output.size(), username_output.begin());
    } else {
        std::copy(username.begin(), username.end(), username_output.begin());
    }
    return CreateNewUser(uuid, username_output);
}

boost::optional<UUID> ProfileManager::GetUser(std::size_t index) const {
    if (index >= MAX_USERS)
        return boost::none;
    return profiles[index].user_uuid;
}

/// Returns a users profile index based on their user id.
boost::optional<std::size_t> ProfileManager::GetUserIndex(const UUID& uuid) const {
    if (!uuid) {
        return boost::none;
    }
    auto iter = std::find_if(profiles.begin(), profiles.end(),
                             [&uuid](const ProfileInfo& p) { return p.user_uuid == uuid; });
    if (iter == profiles.end()) {
        return boost::none;
    }
    return static_cast<std::size_t>(std::distance(profiles.begin(), iter));
}

/// Returns a users profile index based on their profile
boost::optional<std::size_t> ProfileManager::GetUserIndex(const ProfileInfo& user) const {
    return GetUserIndex(user.user_uuid);
}

/// Returns the data structure used by the switch when GetProfileBase is called on acc:*
bool ProfileManager::GetProfileBase(boost::optional<std::size_t> index,
                                    ProfileBase& profile) const {
    if (index == boost::none || index >= MAX_USERS) {
        return false;
    }
    const auto& prof_info = profiles[index.get()];
    profile.user_uuid = prof_info.user_uuid;
    profile.username = prof_info.username;
    profile.timestamp = prof_info.creation_time;
    return true;
}

/// Returns the data structure used by the switch when GetProfileBase is called on acc:*
bool ProfileManager::GetProfileBase(UUID uuid, ProfileBase& profile) const {
    auto idx = GetUserIndex(uuid);
    return GetProfileBase(idx, profile);
}

/// Returns the data structure used by the switch when GetProfileBase is called on acc:*
bool ProfileManager::GetProfileBase(const ProfileInfo& user, ProfileBase& profile) const {
    return GetProfileBase(user.user_uuid, profile);
}

/// Returns the current user count on the system. We keep a variable which tracks the count so we
/// don't have to loop the internal profile array every call.

std::size_t ProfileManager::GetUserCount() const {
    return user_count;
}

/// Lists the current "opened" users on the system. Users are typically not open until they sign
/// into something or pick a profile. As of right now users should all be open until qlaunch is
/// booting

std::size_t ProfileManager::GetOpenUserCount() const {
    return std::count_if(profiles.begin(), profiles.end(),
                         [](const ProfileInfo& p) { return p.is_open; });
}

/// Checks if a user id exists in our profile manager
bool ProfileManager::UserExists(UUID uuid) const {
    return (GetUserIndex(uuid) != boost::none);
}

bool ProfileManager::UserExistsIndex(std::size_t index) const {
    if (index >= MAX_USERS)
        return false;
    return profiles[index].user_uuid.uuid != INVALID_UUID;
}

/// Opens a specific user
void ProfileManager::OpenUser(UUID uuid) {
    auto idx = GetUserIndex(uuid);
    if (idx == boost::none) {
        return;
    }
    profiles[idx.get()].is_open = true;
    last_opened_user = uuid;
}

/// Closes a specific user
void ProfileManager::CloseUser(UUID uuid) {
    auto idx = GetUserIndex(uuid);
    if (idx == boost::none) {
        return;
    }
    profiles[idx.get()].is_open = false;
}

/// Gets all valid user ids on the system
UserIDArray ProfileManager::GetAllUsers() const {
    UserIDArray output;
    std::transform(profiles.begin(), profiles.end(), output.begin(),
                   [](const ProfileInfo& p) { return p.user_uuid; });
    return output;
}

/// Get all the open users on the system and zero out the rest of the data. This is specifically
/// needed for GetOpenUsers and we need to ensure the rest of the output buffer is zero'd out
UserIDArray ProfileManager::GetOpenUsers() const {
    UserIDArray output;
    std::transform(profiles.begin(), profiles.end(), output.begin(), [](const ProfileInfo& p) {
        if (p.is_open)
            return p.user_uuid;
        return UUID{};
    });
    std::stable_partition(output.begin(), output.end(), [](const UUID& uuid) { return uuid; });
    return output;
}

/// Returns the last user which was opened
UUID ProfileManager::GetLastOpenedUser() const {
    return last_opened_user;
}

/// Return the users profile base and the unknown arbitary data.
bool ProfileManager::GetProfileBaseAndData(boost::optional<std::size_t> index, ProfileBase& profile,
                                           ProfileData& data) const {
    if (GetProfileBase(index, profile)) {
        data = profiles[index.get()].data;
        return true;
    }
    return false;
}

/// Return the users profile base and the unknown arbitary data.
bool ProfileManager::GetProfileBaseAndData(UUID uuid, ProfileBase& profile,
                                           ProfileData& data) const {
    auto idx = GetUserIndex(uuid);
    return GetProfileBaseAndData(idx, profile, data);
}

/// Return the users profile base and the unknown arbitary data.
bool ProfileManager::GetProfileBaseAndData(const ProfileInfo& user, ProfileBase& profile,
                                           ProfileData& data) const {
    return GetProfileBaseAndData(user.user_uuid, profile, data);
}

/// Returns if the system is allowing user registrations or not
bool ProfileManager::CanSystemRegisterUser() const {
    return false; // TODO(ogniK): Games shouldn't have
                  // access to user registration, when we
    // emulate qlaunch. Update this to dynamically change.
}

bool ProfileManager::RemoveUser(UUID uuid) {
    auto index = GetUserIndex(uuid);
    if (index == boost::none) {
        return false;
    }

    profiles[*index] = ProfileInfo{};
    std::stable_partition(profiles.begin(), profiles.end(),
                          [](const ProfileInfo& profile) { return profile.user_uuid; });
    return true;
}

bool ProfileManager::SetProfileBase(UUID uuid, const ProfileBase& profile_new) {
    auto index = GetUserIndex(uuid);
    if (profile_new.user_uuid == UUID(INVALID_UUID) || index == boost::none) {
        return false;
    }

    auto& profile = profiles[*index];
    profile.user_uuid = profile_new.user_uuid;
    profile.username = profile_new.username;
    profile.creation_time = profile_new.timestamp;

    return true;
}

void ProfileManager::ParseUserSaveFile() {
    FileUtil::IOFile save(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                              ACC_SAVE_AVATORS_BASE_PATH + "profiles.dat",
                          "rb");

    if (!save.IsOpen()) {
        LOG_WARNING(Service_ACC, "Failed to load profile data from save data... Generating new "
                                 "user 'yuzu' with random UUID.");
        return;
    }

    ProfileDataRaw data;
    if (save.ReadBytes(&data, sizeof(ProfileDataRaw)) != sizeof(ProfileDataRaw)) {
        LOG_WARNING(Service_ACC, "profiles.dat is smaller than expected... Generating new user "
                                 "'yuzu' with random UUID.");
        return;
    }

    for (std::size_t i = 0; i < MAX_USERS; ++i) {
        const auto& user = data.users[i];

        if (user.uuid != UUID(INVALID_UUID))
            AddUser({user.uuid, user.username, user.timestamp, {}, false});
    }

    std::stable_partition(profiles.begin(), profiles.end(),
                          [](const ProfileInfo& profile) { return profile.user_uuid; });
}

void ProfileManager::WriteUserSaveFile() {
    ProfileDataRaw raw{};

    for (std::size_t i = 0; i < MAX_USERS; ++i) {
        raw.users[i].username = profiles[i].username;
        raw.users[i].uuid2 = profiles[i].user_uuid;
        raw.users[i].uuid = profiles[i].user_uuid;
        raw.users[i].timestamp = profiles[i].creation_time;
    }

    const auto raw_path =
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "/system/save/8000000000000010";
    if (FileUtil::Exists(raw_path) && !FileUtil::IsDirectory(raw_path))
        FileUtil::Delete(raw_path);

    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                      ACC_SAVE_AVATORS_BASE_PATH + "profiles.dat";

    if (!FileUtil::CreateFullPath(path)) {
        LOG_WARNING(Service_ACC, "Failed to create full path of profiles.dat. Create the directory "
                                 "nand/system/save/8000000000000010/su/avators to mitigate this "
                                 "issue.");
        return;
    }

    FileUtil::IOFile save(path, "wb");

    if (!save.IsOpen()) {
        LOG_WARNING(Service_ACC, "Failed to write save data to file... No changes to user data "
                                 "made in current session will be saved.");
        return;
    }

    save.Resize(sizeof(ProfileDataRaw));
    save.WriteBytes(&raw, sizeof(ProfileDataRaw));
}

}; // namespace Service::Account
