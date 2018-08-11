// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/acc/acc_su.h"

namespace Service::Account {

ACC_SU::ACC_SU(std::shared_ptr<Module> module, std::shared_ptr<ProfileManager> profile_manager)
    : Module::Interface(std::move(module), std::move(profile_manager), "acc:su") {
    static const FunctionInfo functions[] = {
        {0, &ACC_SU::GetUserCount, "GetUserCount"},
        {1, &ACC_SU::GetUserExistence, "GetUserExistence"},
        {2, &ACC_SU::ListAllUsers, "ListAllUsers"},
        {3, &ACC_SU::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_SU::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_SU::GetProfile, "GetProfile"},
        {6, nullptr, "GetProfileDigest"},
        {50, &ACC_SU::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, nullptr, "TrySelectUserWithoutInteraction"},
        {60, nullptr, "ListOpenContextStoredUsers"},
        {100, nullptr, "GetUserRegistrationNotifier"},
        {101, nullptr, "GetUserStateChangeNotifier"},
        {102, nullptr, "GetBaasAccountManagerForSystemService"},
        {103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
        {104, nullptr, "GetProfileUpdateNotifier"},
        {105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
        {110, nullptr, "StoreSaveDataThumbnail"},
        {111, nullptr, "ClearSaveDataThumbnail"},
        {112, nullptr, "LoadSaveDataThumbnail"},
        {113, nullptr, "GetSaveDataThumbnailExistence"},
        {190, nullptr, "GetUserLastOpenedApplication"},
        {191, nullptr, "ActivateOpenContextHolder"},
        {200, nullptr, "BeginUserRegistration"},
        {201, nullptr, "CompleteUserRegistration"},
        {202, nullptr, "CancelUserRegistration"},
        {203, nullptr, "DeleteUser"},
        {204, nullptr, "SetUserPosition"},
        {205, nullptr, "GetProfileEditor"},
        {206, nullptr, "CompleteUserRegistrationForcibly"},
        {210, nullptr, "CreateFloatingRegistrationRequest"},
        {230, nullptr, "AuthenticateServiceAsync"},
        {250, nullptr, "GetBaasAccountAdministrator"},
        {290, nullptr, "ProxyProcedureForGuestLoginWithNintendoAccount"},
        {291, nullptr, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
        {299, nullptr, "SuspendBackgroundDaemon"},
        {997, nullptr, "DebugInvalidateTokenCacheForUser"},
        {998, nullptr, "DebugSetUserStateClose"},
        {999, nullptr, "DebugSetUserStateOpen"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::Account
