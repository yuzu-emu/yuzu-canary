// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace Service::LBL {

class LBL final : public ServiceFramework<LBL> {
public:
    explicit LBL() : ServiceFramework{"lbl"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LBL::SaveCurrentSetting, "SaveCurrentSetting"},
            {1, &LBL::LoadCurrentSetting, "LoadCurrentSetting"},
            {2, &LBL::SetCurrentBrightnessSetting, "SetCurrentBrightnessSetting"},
            {3, &LBL::GetCurrentBrightnessSetting, "GetCurrentBrightnessSetting"},
            {4, &LBL::ApplyCurrentBrightnessSettingToBacklight, "ApplyCurrentBrightnessSettingToBacklight"},
            {5, &LBL::GetBrightnessSettingAppliedToBacklight, "GetBrightnessSettingAppliedToBacklight"},
            {6, &LBL::SwitchBacklightOn, "SwitchBacklightOn"},
            {7, &LBL::SwitchBacklightOff, "SwitchBacklightOff"},
            {8, &LBL::GetBacklightSwitchStatus, "GetBacklightSwitchStatus"},
            {9, &LBL::EnableDimming, "EnableDimming"},
            {10, &LBL::DisableDimming, "DisableDimming"},
            {11, &LBL::IsDimmingEnabled, "IsDimmingEnabled"},
            {12, &LBL::EnableAutoBrightnessControl, "EnableAutoBrightnessControl"},
            {13, &LBL::DisableAutoBrightnessControl, "DisableAutoBrightnessControl"},
            {14, &LBL::IsAutoBrightnessControlEnabled, "IsAutoBrightnessControlEnabled"},
            {15, nullptr, "SetAmbientLightSensorValue"},
            {16, nullptr, "GetAmbientLightSensorValue"},
            {17, nullptr, "SetBrightnessReflectionDelayLevel"},
            {18, nullptr, "GetBrightnessReflectionDelayLevel"},
            {19, nullptr, "SetCurrentBrightnessMapping"},
            {20, nullptr, "GetCurrentBrightnessMapping"},
            {21, nullptr, "SetCurrentAmbientLightSensorMapping"},
            {22, nullptr, "GetCurrentAmbientLightSensorMapping"},
            {23, nullptr, "IsAmbientLightSensorAvailable"},
            {24, &LBL::SetCurrentBrightnessSettingForVrMode, "SetCurrentBrightnessSettingForVrMode"},
            {25, &LBL::GetCurrentBrightnessSettingForVrMode, "GetCurrentBrightnessSettingForVrMode"},
            {26, &LBL::EnableVrMode, "EnableVrMode"},
            {27, &LBL::DisableVrMode, "DisableVrMode"},
            {28, &LBL::IsVrModeEnabled, "IsVrModeEnabled"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void LoadFromSettings() {
        current_brightness = Settings::values.backlight_brightness;
        current_vr_mode_brightness = Settings::values.backlight_brightness;

        if (auto_brightness_enabled) {
            return;
        }

        if (vr_mode_enabled) {
            Renderer().SetCurrentBrightness(current_vr_mode_brightness);
        } else {
            Renderer().SetCurrentBrightness(current_brightness);
        }
    }

private:
    f32 GetAutoBrightnessValue() const {
        return 0.5f;
    }

    VideoCore::RendererBase& Renderer() {
        return Core::System::GetInstance().Renderer();
    }

    void SaveCurrentSetting(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        Settings::values.backlight_brightness = current_brightness;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void LoadCurrentSetting(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        LoadFromSettings();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetCurrentBrightnessSetting(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto value = rp.PopRaw<f32>();

        LOG_DEBUG(Service_LBL, "called, value={:.3f}", value);

        current_brightness = std::clamp(value, 0.0f, 1.0f);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetCurrentBrightnessSetting(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(current_brightness);
    }

    void ApplyCurrentBrightnessSettingToBacklight(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        if (!auto_brightness_enabled) {
            Renderer().SetCurrentBrightness(vr_mode_enabled ? current_vr_mode_brightness
                                                            : current_brightness);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetBrightnessSettingAppliedToBacklight(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(Renderer().GetCurrentResultantBrightness());
    }

    void SwitchBacklightOn(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.PopRaw<u64>();

        LOG_DEBUG(Service_LBL, "called, fade_time={:016X}", fade_time);

        Renderer().SetBacklightStatus(true, fade_time);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SwitchBacklightOff(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.PopRaw<u64>();

        LOG_DEBUG(Service_LBL, "called, fade_time={:016X}", fade_time);

        Renderer().SetBacklightStatus(false, fade_time);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetBacklightSwitchStatus(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u8>(Renderer().GetBacklightStatus());
    }

    void EnableDimming(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        dimming_enabled = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void DisableDimming(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "callled");

        dimming_enabled = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void IsDimmingEnabled(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u8>(dimming_enabled);
    }

    void EnableAutoBrightnessControl(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        auto_brightness_enabled = true;
        Renderer().SetCurrentBrightness(GetAutoBrightnessValue());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void DisableAutoBrightnessControl(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        auto_brightness_enabled = false;
        Renderer().SetCurrentBrightness(current_brightness);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void IsAutoBrightnessControlEnabled(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u8>(auto_brightness_enabled);
    }

    void SetCurrentBrightnessSettingForVrMode(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto value = rp.PopRaw<f32>();

        LOG_DEBUG(Service_LBL, "called, value={:.3f}", value);

        current_vr_mode_brightness = std::clamp(value, 0.0f, 1.0f);

        if (vr_mode_enabled && !auto_brightness_enabled) {
            Renderer().SetCurrentBrightness(value);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetCurrentBrightnessSettingForVrMode(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(current_vr_mode_brightness);
    }

    void EnableVrMode(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        if (!vr_mode_enabled && !auto_brightness_enabled &&
            current_brightness != current_vr_mode_brightness) {
            Renderer().SetCurrentBrightness(current_vr_mode_brightness);
        }

        vr_mode_enabled = true;
    }

    void DisableVrMode(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);

        if (vr_mode_enabled && !auto_brightness_enabled &&
            current_brightness != current_vr_mode_brightness) {
            Renderer().SetCurrentBrightness(current_brightness);
        }

        vr_mode_enabled = false;
    }

    void IsVrModeEnabled(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push(vr_mode_enabled);
    }

    bool auto_brightness_enabled = false;
    bool dimming_enabled = true;

    f32 current_brightness = GetAutoBrightnessValue();
    f32 current_vr_mode_brightness = GetAutoBrightnessValue();

    bool vr_mode_enabled = false;
};

void RequestLoadCurrentSetting(SM::ServiceManager& sm) {
    if (&sm == nullptr) {
        return;
    }

    const auto lbl = sm.GetService<LBL>("lbl");

    if (lbl) {
        lbl->LoadFromSettings();
    }
}

void InstallInterfaces(SM::ServiceManager& sm) {
    std::make_shared<LBL>()->InstallAsService(sm);
}

} // namespace Service::LBL
