// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"

#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/loader/loader.h"
#include "core/settings.h"
#include "core/telemetry_session.h"

#ifdef ENABLE_WEB_SERVICE
#include "web_service/telemetry_json.h"
#include "web_service/verify_login.h"
#endif

namespace Core {

static u64 GenerateTelemetryId() {
    u64 telemetry_id{};

    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_context ctr_drbg;
    constexpr std::array<char, 18> personalization{{"yuzu Telemetry ID"}};

    mbedtls_ctr_drbg_init(&ctr_drbg);
    ASSERT(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 reinterpret_cast<const unsigned char*>(personalization.data()),
                                 personalization.size()) == 0);
    ASSERT(mbedtls_ctr_drbg_random(&ctr_drbg, reinterpret_cast<unsigned char*>(&telemetry_id),
                                   sizeof(u64)) == 0);

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return telemetry_id;
}

u64 GetTelemetryId() {
    u64 telemetry_id{};
    const std::string filename{FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) +
                               "telemetry_id"};

    bool generate_new_id = !FileUtil::Exists(filename);
    if (!generate_new_id) {
        FileUtil::IOFile file(filename, "rb");
        if (!file.IsOpen()) {
            LOG_ERROR(Core, "failed to open telemetry_id: {}", filename);
            return {};
        }
        file.ReadBytes(&telemetry_id, sizeof(u64));
        if (telemetry_id == 0) {
            LOG_ERROR(Frontend, "telemetry_id is 0. Generating a new one.", telemetry_id);
            generate_new_id = true;
        }
    }

    if (generate_new_id) {
        FileUtil::IOFile file(filename, "wb");
        if (!file.IsOpen()) {
            LOG_ERROR(Core, "failed to open telemetry_id: {}", filename);
            return {};
        }
        telemetry_id = GenerateTelemetryId();
        file.WriteBytes(&telemetry_id, sizeof(u64));
    }

    return telemetry_id;
}

u64 RegenerateTelemetryId() {
    const u64 new_telemetry_id{GenerateTelemetryId()};
    const std::string filename{FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) +
                               "telemetry_id"};

    FileUtil::IOFile file(filename, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Core, "failed to open telemetry_id: {}", filename);
        return {};
    }
    file.WriteBytes(&new_telemetry_id, sizeof(u64));
    return new_telemetry_id;
}

bool VerifyLogin(const std::string& username, const std::string& token) {
#ifdef ENABLE_WEB_SERVICE
    return WebService::VerifyLogin(Settings::values.web_api_url, username, token);
#else
    return false;
#endif
}

TelemetrySession::TelemetrySession() {
#ifdef ENABLE_WEB_SERVICE
    if (Settings::values.enable_telemetry) {
        backend = std::make_unique<WebService::TelemetryJson>(Settings::values.web_api_url,
                                                              Settings::values.yuzu_username,
                                                              Settings::values.yuzu_token);
    } else {
        backend = std::make_unique<Telemetry::NullVisitor>();
    }
#else
    backend = std::make_unique<Telemetry::NullVisitor>();
#endif
    // Log one-time top-level information
    AddField(Telemetry::FieldType::None, "TelemetryId", GetTelemetryId());

    // Log one-time session start information
    const s64 init_time{std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()};
    AddField(Telemetry::FieldType::Session, "Init_Time", init_time);

    u64 program_id{};
    const Loader::ResultStatus res{System::GetInstance().GetAppLoader().ReadProgramId(program_id)};
    if (res == Loader::ResultStatus::Success) {
        const std::string formatted_program_id{fmt::format("{:016X}", program_id)};
        AddField(Telemetry::FieldType::Session, "ProgramId", formatted_program_id);

        std::string name;
        System::GetInstance().GetAppLoader().ReadTitle(name);

        if (name.empty()) {
            auto [nacp, icon_file] = FileSys::PatchManager(program_id).GetControlMetadata();
            if (nacp != nullptr)
                name = nacp->GetApplicationName();
        }

        if (!name.empty())
            AddField(Telemetry::FieldType::Session, "ProgramName", name);
    }

    AddField(Telemetry::FieldType::Session, "ProgramFormat",
             static_cast<u8>(System::GetInstance().GetAppLoader().GetFileType()));

    // Log application information
    Telemetry::AppendBuildInfo(field_collection);

    // Log user system information
    Telemetry::AppendCPUInfo(field_collection);
    Telemetry::AppendOSInfo(field_collection);

    // Log user configuration information
    AddField(Telemetry::FieldType::UserConfig, "Audio_SinkId", Settings::values.sink_id);
    AddField(Telemetry::FieldType::UserConfig, "Audio_EnableAudioStretching",
             Settings::values.enable_audio_stretching);
    AddField(Telemetry::FieldType::UserConfig, "Core_UseCpuJit", Settings::values.use_cpu_jit);
    AddField(Telemetry::FieldType::UserConfig, "Core_UseMultiCore",
             Settings::values.use_multi_core);
    AddField(Telemetry::FieldType::UserConfig, "Renderer_ResolutionFactor",
             Settings::values.resolution_factor);
    AddField(Telemetry::FieldType::UserConfig, "Renderer_UseFrameLimit",
             Settings::values.use_frame_limit);
    AddField(Telemetry::FieldType::UserConfig, "Renderer_FrameLimit", Settings::values.frame_limit);
    AddField(Telemetry::FieldType::UserConfig, "Renderer_UseAccurateGpuEmulation",
             Settings::values.use_accurate_gpu_emulation);
    AddField(Telemetry::FieldType::UserConfig, "System_UseDockedMode",
             Settings::values.use_docked_mode);
}

TelemetrySession::~TelemetrySession() {
    // Log one-time session end information
    const s64 shutdown_time{std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count()};
    AddField(Telemetry::FieldType::Session, "Shutdown_Time", shutdown_time);

    // Complete the session, submitting to web service if necessary
    // This is just a placeholder to wrap up the session once the core completes and this is
    // destroyed. This will be moved elsewhere once we are actually doing real I/O with the service.
    field_collection.Accept(*backend);
    backend->Complete();
    backend = nullptr;
}

} // namespace Core
