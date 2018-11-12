// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/backend.h"
#include "common/string_util.h"
#include "core/frontend/applets/software_keyboard.h"

namespace Core::Frontend {
SoftwareKeyboardApplet::~SoftwareKeyboardApplet() = default;

std::optional<std::u16string> DefaultSoftwareKeyboardApplet::GetText(
    SoftwareKeyboardParameters parameters) const {
    if (parameters.initial_text.empty())
        return u"yuzu";

    return parameters.initial_text;
}

void DefaultSoftwareKeyboardApplet::SendTextCheckDialog(std::u16string error_message) const {
    LOG_WARNING(Service_AM,
                "(STUBBED) called - Default fallback software keyboard does not support text "
                "check! (error_message={})",
                Common::UTF16ToUTF8(error_message));
}
} // namespace Core::Frontend
