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
#include "core/constants.h"
#include "core/core_timing.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/acc_aa.h"
#include "core/hle/service/acc/acc_su.h"
#include "core/hle/service/acc/acc_u0.h"
#include "core/hle/service/acc/acc_u1.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/glue/arp.h"
#include "core/hle/service/glue/manager.h"
#include "core/hle/service/sm/sm.h"
#include "core/loader/loader.h"

namespace Service::Account {

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
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(SanitizeJPEGSize(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
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
            rb.Push(SanitizeJPEGSize(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
        } else {
            rb.Push(SanitizeJPEGSize(image.GetSize()));
        }
    }

    const ProfileManager& profile_manager;
    Common::UUID user_id; ///< The user id this profile refers to.
};

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    IManagerForApplication() : ServiceFramework("IManagerForApplication") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            {1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            {2, nullptr, "EnsureIdTokenCacheAsync"},
            {3, nullptr, "LoadIdTokenCache"},
            {130, nullptr, "GetNintendoAccountUserResourceCacheForApplication"},
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, nullptr, "StoreOpenContext"},
            {170, nullptr, "LoadNetworkServiceLicenseKindAsync"},
        };
        // clang-format on

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
    IPC::RequestParser rp{ctx};
    auto pid = rp.Pop<u64>();

    LOG_DEBUG(Service_ACC, "called, process_id={}", pid);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase(pid));
}

void Module::Interface::InitializeApplicationInfoRestricted(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto pid = rp.Pop<u64>();

    LOG_WARNING(Service_ACC, "(Partial implementation) called, process_id={}", pid);

    // TODO(ogniK): We require checking if the user actually owns the title and what not. As of
    // currently, we assume the user owns the title. InitializeApplicationInfoBase SHOULD be called
    // first then we do extra checks if the game is a digital copy.

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase(pid));
}

ResultCode Module::Interface::InitializeApplicationInfoBase(u64 process_id) {
    if (application_info) {
        LOG_ERROR(Service_ACC, "Application already initialized");
        return ERR_ACCOUNTINFO_ALREADY_INITIALIZED;
    }

    const auto& list = system.Kernel().GetProcessList();
    const auto iter = std::find_if(list.begin(), list.end(), [&process_id](const auto& process) {
        return process->GetProcessID() == process_id;
    });

    if (iter == list.end()) {
        LOG_ERROR(Service_ACC, "Failed to find process ID");
        application_info.application_type = ApplicationType::Unknown;

        return ERR_ACCOUNTINFO_BAD_APPLICATION;
    }

    const auto launch_property = system.GetARPManager().GetLaunchProperty((*iter)->GetTitleID());

    if (launch_property.Failed()) {
        LOG_ERROR(Service_ACC, "Failed to get launch property");
        return ERR_ACCOUNTINFO_BAD_APPLICATION;
    }

    switch (launch_property->base_game_storage_id) {
    case FileSys::StorageId::GameCard:
        application_info.application_type = ApplicationType::GameCard;
        break;
    case FileSys::StorageId::Host:
    case FileSys::StorageId::NandUser:
    case FileSys::StorageId::SdCard:
        application_info.application_type = ApplicationType::Digital;
        break;
    default:
        LOG_ERROR(Service_ACC, "Invalid game storage ID");
        return ERR_ACCOUNTINFO_BAD_APPLICATION;
    }

    LOG_WARNING(Service_ACC, "ApplicationInfo init required");
    // TODO(ogniK): Actual initalization here

    return RESULT_SUCCESS;
}

void Module::Interface::GetBaasAccountManagerForApplication(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IManagerForApplication>();
}

void Module::Interface::IsUserAccountSwitchLocked(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    FileSys::NACP nacp;
    const auto res = system.GetAppLoader().ReadControlData(nacp);

    bool is_locked = false;

    if (res != Loader::ResultStatus::Success) {
        FileSys::PatchManager pm{system.CurrentProcess()->GetTitleID()};
        auto nacp_unique = pm.GetControlMetadata().first;

        if (nacp_unique != nullptr) {
            is_locked = nacp_unique->GetUserAccountSwitchLock();
        } else {
            LOG_ERROR(Service_ACC, "nacp_unique is null!");
        }
    } else {
        is_locked = nacp.GetUserAccountSwitchLock();
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(is_locked);
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
                             std::shared_ptr<ProfileManager> profile_manager, Core::System& system,
                             const char* name)
    : ServiceFramework(name), module(std::move(module)),
      profile_manager(std::move(profile_manager)), system(system) {}

Module::Interface::~Interface() = default;

void InstallInterfaces(Core::System& system) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();

    std::make_shared<ACC_AA>(module, profile_manager, system)
        ->InstallAsService(system.ServiceManager());
    std::make_shared<ACC_SU>(module, profile_manager, system)
        ->InstallAsService(system.ServiceManager());
    std::make_shared<ACC_U0>(module, profile_manager, system)
        ->InstallAsService(system.ServiceManager());
    std::make_shared<ACC_U1>(module, profile_manager, system)
        ->InstallAsService(system.ServiceManager());
}

} // namespace Service::Account
