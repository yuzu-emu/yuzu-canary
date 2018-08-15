// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/acc/acc_u1.h"

namespace Service::Account {

ACC_U1::ACC_U1(std::shared_ptr<Module> module, std::shared_ptr<ProfileManager> profile_manager)
    : Module::Interface(std::move(module), std::move(profile_manager), "acc:u1") {
    static const FunctionInfo functions[] = {
        {0, &ACC_U1::GetUserCount, "GetUserCount"},
        {1, &ACC_U1::GetUserExistence, "GetUserExistence"},
        {2, &ACC_U1::ListAllUsers, "ListAllUsers"},
        {3, &ACC_U1::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_U1::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_U1::GetProfile, "GetProfile"},
        {6, nullptr, "GetProfileDigest"},
        {50, &ACC_U1::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, nullptr, "TrySelectUserWithoutInteraction"},
        {60, nullptr, "ListOpenContextStoredUsers"},
        {100, nullptr, "GetUserRegistrationNotifier"},
        {101, nullptr, "GetUserStateChangeNotifier"},
        {102, nullptr, "GetBaasAccountManagerForSystemService"},
        {103, nullptr, "GetProfileUpdateNotifier"},
        {104, nullptr, "CheckNetworkServiceAvailabilityAsync"},
        {105, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
        {110, nullptr, "StoreSaveDataThumbnail"},
        {111, nullptr, "ClearSaveDataThumbnail"},
        {112, nullptr, "LoadSaveDataThumbnail"},
        {113, nullptr, "GetSaveDataThumbnailExistence"},
        {190, nullptr, "GetUserLastOpenedApplication"},
        {191, nullptr, "ActivateOpenContextHolder"},
        {997, nullptr, "DebugInvalidateTokenCacheForUser"},
        {998, nullptr, "DebugSetUserStateClose"},
        {999, nullptr, "DebugSetUserStateOpen"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::Account
