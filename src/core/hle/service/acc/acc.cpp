// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/acc_aa.h"
#include "core/hle/service/acc/acc_su.h"
#include "core/hle/service/acc/acc_u0.h"
#include "core/hle/service/acc/acc_u1.h"
#include "core/hle/service/acc/profile_manager.h"

namespace Service::Account {

// Smallest JPEG https://github.com/mathiasbynens/small/blob/master/jpeg.jpg
// used as a backup should the one on disk not exist
constexpr u32 backup_jpeg_size = 107;
constexpr std::array<u8, backup_jpeg_size> backup_jpeg{{
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02,
    0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 0x06, 0x06, 0x05,
    0x06, 0x09, 0x08, 0x0a, 0x0a, 0x09, 0x08, 0x09, 0x09, 0x0a, 0x0c, 0x0f, 0x0c, 0x0a, 0x0b, 0x0e,
    0x0b, 0x09, 0x09, 0x0d, 0x11, 0x0d, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x10, 0x0a, 0x0c, 0x12, 0x13,
    0x12, 0x10, 0x13, 0x0f, 0x10, 0x10, 0x10, 0xff, 0xc9, 0x00, 0x0b, 0x08, 0x00, 0x01, 0x00, 0x01,
    0x01, 0x01, 0x11, 0x00, 0xff, 0xcc, 0x00, 0x06, 0x00, 0x10, 0x10, 0x05, 0xff, 0xda, 0x00, 0x08,
    0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0xd2, 0xcf, 0x20, 0xff, 0xd9,
}};

static std::string GetImagePath(Common::UUID uuid) {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
           "/system/save/8000000000000010/su/avators/" + uuid.FormatSwitch() + ".jpg";
}

static constexpr u32 SanitizeJPEGSize(std::size_t size) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    return static_cast<u32>(std::min(size, max_jpeg_image_size));
}

class IProfile final : public ServiceFramework<IProfile> {
public:
    explicit IProfile(Common::UUID user_id, ProfileManager& profile_manager)
        : ServiceFramework("IProfile"), profile_manager(profile_manager), user_id(user_id) {
        static const FunctionInfo functions[] = {
            {0, &IProfile::Get, "Get"},
            {1, &IProfile::GetBase, "GetBase"},
            {10, &IProfile::GetImageSize, "GetImageSize"},
            {11, &IProfile::LoadImage, "LoadImage"},
        };
        RegisterHandlers(functions);
    }

private:
    void Get(Kernel::HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called user_id={}", user_id.Format());
        ProfileBase profile_base{};
        ProfileData data{};
        if (profile_manager.GetProfileBaseAndData(user_id, profile_base, data)) {
            std::array<u8, sizeof(ProfileData)> raw_data;
            std::memcpy(raw_data.data(), &data, sizeof(ProfileData));
            ctx.WriteBuffer(raw_data);
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(RESULT_SUCCESS);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base and data for user={}",
                      user_id.Format());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(-1)); // TODO(ogniK): Get actual error code
        }
    }

    void GetBase(Kernel::HLERequestContext& ctx) {
        LOG_INFO(Service_ACC, "called user_id={}", user_id.Format());
        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(RESULT_SUCCESS);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base for user={}", user_id.Format());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(-1)); // TODO(ogniK): Get actual error code
        }
    }

    void LoadImage(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);

        const FileUtil::IOFile image(GetImagePath(user_id), "rb");
        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            ctx.WriteBuffer(backup_jpeg);
            rb.Push<u32>(backup_jpeg_size);
            return;
        }

        const u32 size = SanitizeJPEGSize(image.GetSize());
        std::vector<u8> buffer(size);
        image.ReadBytes(buffer.data(), buffer.size());

        ctx.WriteBuffer(buffer.data(), buffer.size());
        rb.Push<u32>(size);
    }

    void GetImageSize(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);

        const FileUtil::IOFile image(GetImagePath(user_id), "rb");

        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            rb.Push<u32>(backup_jpeg_size);
        } else {
            rb.Push<u32>(SanitizeJPEGSize(image.GetSize()));
        }
    }

    const ProfileManager& profile_manager;
    Common::UUID user_id; ///< The user id this profile refers to.
};

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    IManagerForApplication() : ServiceFramework("IManagerForApplication") {
        static const FunctionInfo functions[] = {
            {0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            {1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            {2, nullptr, "EnsureIdTokenCacheAsync"},
            {3, nullptr, "LoadIdTokenCache"},
            {130, nullptr, "GetNintendoAccountUserResourceCacheForApplication"},
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, nullptr, "StoreOpenContext"},
        };
        RegisterHandlers(functions);
    }

private:
    void CheckAvailability(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(false); // TODO: Check when this is supposed to return true and when not
    }

    void GetAccountId(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        // Should return a nintendo account ID
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u64>(1);
    }
};

void Module::Interface::GetUserCount(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(static_cast<u32>(profile_manager->GetUserCount()));
}

void Module::Interface::GetUserExistence(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_INFO(Service_ACC, "called user_id={}", user_id.Format());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(profile_manager->UserExists(user_id));
}

void Module::Interface::ListAllUsers(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Module::Interface::ListOpenUsers(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Module::Interface::GetLastOpenedUser(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<Common::UUID>(profile_manager->GetLastOpenedUser());
}

void Module::Interface::GetProfile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id={}", user_id.Format());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IProfile>(user_id, *profile_manager);
}

void Module::Interface::IsUserRegistrationRequestPermitted(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(profile_manager->CanSystemRegisterUser());
}

void Module::Interface::InitializeApplicationInfo(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void Module::Interface::GetBaasAccountManagerForApplication(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IManagerForApplication>();
}

void Module::Interface::TrySelectUserWithoutInteraction(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u128>(Common::INVALID_UUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::all_of(user_list.begin(), user_list.end(),
                    [](const auto& user) { return user.uuid == Common::INVALID_UUID; })) {
        rb.Push(ResultCode(-1)); // TODO(ogniK): Find the correct error code
        rb.PushRaw<u128>(Common::INVALID_UUID);
        return;
    }

    // Select the first user we have
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<u128>(profile_manager->GetUser(0)->uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module,
                             std::shared_ptr<ProfileManager> profile_manager, const char* name)
    : ServiceFramework(name), module(std::move(module)),
      profile_manager(std::move(profile_manager)) {}

Module::Interface::~Interface() = default;

void InstallInterfaces(SM::ServiceManager& service_manager) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();
    std::make_shared<ACC_AA>(module, profile_manager)->InstallAsService(service_manager);
    std::make_shared<ACC_SU>(module, profile_manager)->InstallAsService(service_manager);
    std::make_shared<ACC_U0>(module, profile_manager)->InstallAsService(service_manager);
    std::make_shared<ACC_U1>(module, profile_manager)->InstallAsService(service_manager);
}

} // namespace Service::Account
