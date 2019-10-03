// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "core/core.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/sm/sm.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"

namespace Settings {

namespace NativeButton {
const std::array<const char*, NumButtons> mapping = {{
    "button_a",
    "button_b",
    "button_x",
    "button_y",
    "button_lstick",
    "button_rstick",
    "button_l",
    "button_r",
    "button_zl",
    "button_zr",
    "button_plus",
    "button_minus",
    "button_dleft",
    "button_dup",
    "button_dright",
    "button_ddown",
    "button_lstick_left",
    "button_lstick_up",
    "button_lstick_right",
    "button_lstick_down",
    "button_rstick_left",
    "button_rstick_up",
    "button_rstick_right",
    "button_rstick_down",
    "button_sl",
    "button_sr",
    "button_home",
    "button_screenshot",
}};
}

namespace NativeAnalog {
const std::array<const char*, NumAnalogs> mapping = {{
    "lstick",
    "rstick",
}};
}

namespace NativeMouseButton {
const std::array<const char*, NumMouseButtons> mapping = {{
    "left",
    "right",
    "middle",
    "forward",
    "back",
}};
}

Values values = {};

void Apply() {
    GDBStub::SetServerPort(values.gdbstub_port);
    GDBStub::ToggleServer(values.use_gdbstub);

    auto& system_instance = Core::System::GetInstance();
    if (system_instance.IsPoweredOn()) {
        system_instance.Renderer().RefreshBaseSettings();
        Service::LBL::RequestLoadCurrentSetting(system_instance.ServiceManager());
    }

    Service::HID::ReloadInputDevices();
}

template <typename T>
void LogSetting(const std::string& name, const T& value) {
    LOG_INFO(Config, "{}: {}", name, value);
}

void LogSettings() {
    LOG_INFO(Config, "yuzu Configuration:");
    LogSetting("System_UseDockedMode", Settings::values.use_docked_mode);
    LogSetting("System_RngSeed", Settings::values.rng_seed.value_or(0));
    LogSetting("System_CurrentUser", Settings::values.current_user);
    LogSetting("System_LanguageIndex", Settings::values.language_index);
    LogSetting("Core_UseMultiCore", Settings::values.use_multi_core);
    LogSetting("Renderer_UseResolutionFactor", Settings::values.resolution_factor);
    LogSetting("Renderer_UseFrameLimit", Settings::values.use_frame_limit);
    LogSetting("Renderer_FrameLimit", Settings::values.frame_limit);
    LogSetting("Renderer_UseDiskShaderCache", Settings::values.use_disk_shader_cache);
    LogSetting("Renderer_UseAccurateGpuEmulation", Settings::values.use_accurate_gpu_emulation);
    LogSetting("Renderer_UseAsynchronousGpuEmulation",
               Settings::values.use_asynchronous_gpu_emulation);
    LogSetting("Audio_OutputEngine", Settings::values.sink_id);
    LogSetting("Audio_EnableAudioStretching", Settings::values.enable_audio_stretching);
    LogSetting("Audio_OutputDevice", Settings::values.audio_device_id);
    LogSetting("DataStorage_UseVirtualSd", Settings::values.use_virtual_sd);
    LogSetting("DataStorage_NandDir", FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
    LogSetting("DataStorage_SdmcDir", FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir));
    LogSetting("Debugging_UseGdbstub", Settings::values.use_gdbstub);
    LogSetting("Debugging_GdbstubPort", Settings::values.gdbstub_port);
    LogSetting("Debugging_ProgramArgs", Settings::values.program_args);
    LogSetting("Services_BCATBackend", Settings::values.bcat_backend);
    LogSetting("Services_BCATBoxcatLocal", Settings::values.bcat_boxcat_local);
}

} // namespace Settings
