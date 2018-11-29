// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>

#include "common/assert.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/profile_select.h"

namespace Service::AM::Applets {

constexpr ResultCode ERR_USER_CANCELLED_SELECTION{ErrorModule::Account, 1};

ProfileSelect::ProfileSelect() = default;
ProfileSelect::~ProfileSelect() = default;

void ProfileSelect::Initialize() {
    complete = false;
    status = RESULT_SUCCESS;
    final_data.clear();

    Applet::Initialize();

    const auto user_config_storage = broker.PopNormalDataToApplet();
    ASSERT(user_config_storage != nullptr);
    const auto& user_config = user_config_storage->GetData();

    ASSERT(user_config.size() >= sizeof(UserSelectionConfig));
    std::memcpy(&config, user_config.data(), sizeof(UserSelectionConfig));
}

bool ProfileSelect::TransactionComplete() const {
    return complete;
}

ResultCode ProfileSelect::GetStatus() const {
    return status;
}

void ProfileSelect::ExecuteInteractive() {
    UNREACHABLE_MSG("Attempted to call interactive execution on non-interactive applet.");
}

void ProfileSelect::Execute() {
    if (complete) {
        broker.PushNormalDataFromApplet(IStorage{final_data});
        return;
    }

    const auto& frontend{Core::System::GetInstance().GetProfileSelector()};

    frontend.SelectProfile([this](std::optional<Account::UUID> uuid) { SelectionComplete(uuid); });
}

void ProfileSelect::SelectionComplete(std::optional<Account::UUID> uuid) {
    UserSelectionOutput output{};

    if (uuid.has_value() && uuid->uuid != Account::INVALID_UUID) {
        output.result = 0;
        output.uuid_selected = uuid->uuid;
    } else {
        status = ERR_USER_CANCELLED_SELECTION;
        output.result = ERR_USER_CANCELLED_SELECTION.raw;
        output.uuid_selected = Account::INVALID_UUID;
    }

    final_data = std::vector<u8>(sizeof(UserSelectionOutput));
    std::memcpy(final_data.data(), &output, final_data.size());
    broker.PushNormalDataFromApplet(IStorage{final_data});
    broker.SignalStateChanged();
}

} // namespace Service::AM::Applets
