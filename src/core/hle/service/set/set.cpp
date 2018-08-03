// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include "common/logging/log.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/service/set/set.h"
#include "core/settings.h"

namespace Service::Set {

constexpr std::array<LanguageCode, 17> available_language_codes = {{
    LanguageCode::JA,
    LanguageCode::EN_US,
    LanguageCode::FR,
    LanguageCode::DE,
    LanguageCode::IT,
    LanguageCode::ES,
    LanguageCode::ZH_CN,
    LanguageCode::KO,
    LanguageCode::NL,
    LanguageCode::PT,
    LanguageCode::RU,
    LanguageCode::ZH_TW,
    LanguageCode::EN_GB,
    LanguageCode::FR_CA,
    LanguageCode::ES_419,
    LanguageCode::ZH_HANS,
    LanguageCode::ZH_HANT,
}};

LanguageCode GetLanguageCodeFromIndex(size_t index) {
    return available_language_codes.at(index);
}

void SET::GetAvailableLanguageCodes(Kernel::HLERequestContext& ctx) {
    ctx.WriteBuffer(available_language_codes);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u32>(available_language_codes.size()));

    LOG_DEBUG(Service_SET, "called");
}

void SET::GetAvailableLanguageCodeCount(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u32>(available_language_codes.size()));

    LOG_DEBUG(Service_SET, "called");
}

void SET::GetLanguageCode(Kernel::HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.Push(static_cast<u64>(available_language_codes[Settings::values.language_index]));

    LOG_DEBUG(Service_SET, "called {}", Settings::values.language_index);
}

SET::SET() : ServiceFramework("set") {
    static const FunctionInfo functions[] = {
        {0, &SET::GetLanguageCode, "GetLanguageCode"},
        {1, &SET::GetAvailableLanguageCodes, "GetAvailableLanguageCodes"},
        {2, nullptr, "MakeLanguageCode"},
        {3, &SET::GetAvailableLanguageCodeCount, "GetAvailableLanguageCodeCount"},
        {4, nullptr, "GetRegionCode"},
        {5, &SET::GetAvailableLanguageCodes, "GetAvailableLanguageCodes2"},
        {6, &SET::GetAvailableLanguageCodeCount, "GetAvailableLanguageCodeCount2"},
        {7, nullptr, "GetKeyCodeMap"},
        {8, nullptr, "GetQuestFlag"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::Set
