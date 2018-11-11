// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/backend.h"
#include "common/string_util.h"
#include "core/frontend/applets/software_keyboard.h"

namespace Frontend {
bool DefaultSoftwareKeyboardApplet::GetText(Parameters parameters, std::u16string& text) {
    if (parameters.initial_text.empty())
        text = Common::UTF8ToUTF16("yuzu");
    else
        text = parameters.initial_text;

    return true;
}
} // namespace Frontend
