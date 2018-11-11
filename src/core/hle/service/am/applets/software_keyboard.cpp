// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/string_util.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/software_keyboard.h"

namespace Service::AM::Applets {

constexpr std::size_t SWKBD_OUTPUT_BUFFER_SIZE = 0x7D8;
constexpr std::size_t DEFAULT_MAX_LENGTH = 500;

static Frontend::SoftwareKeyboardApplet::Parameters ConvertToFrontendParameters(
    KeyboardConfig config, std::u16string initial_text) {
    Frontend::SoftwareKeyboardApplet::Parameters params{};

    params.submit_text = Common::UTF16StringFromFixedZeroTerminatedBuffer(
        config.submit_text.data(), config.submit_text.size());
    params.header_text = Common::UTF16StringFromFixedZeroTerminatedBuffer(
        config.header_text.data(), config.header_text.size());
    params.sub_text = Common::UTF16StringFromFixedZeroTerminatedBuffer(config.sub_text.data(),
                                                                       config.sub_text.size());
    params.guide_text = Common::UTF16StringFromFixedZeroTerminatedBuffer(config.guide_text.data(),
                                                                         config.guide_text.size());
    params.initial_text = initial_text;
    params.max_length = config.length_limit == 0 ? DEFAULT_MAX_LENGTH : config.length_limit;
    params.password = static_cast<bool>(config.is_password);
    params.cursor_at_beginning = static_cast<bool>(config.initial_cursor_position);
    params.value = static_cast<u8>(config.keyset_disable_bitmask);

    return params;
}

void SoftwareKeyboard::Initialize(std::vector<std::shared_ptr<IStorage>> storage_) {
    Applet::Initialize(std::move(storage_));

    ASSERT(storage_stack.size() >= 2);
    const auto& keyboard_config = storage_stack[1]->GetData();
    ASSERT(keyboard_config.size() >= sizeof(KeyboardConfig));
    std::memcpy(&config, keyboard_config.data(), sizeof(KeyboardConfig));

    ASSERT_MSG(config.text_check == 0, "Text check software keyboard mode is not implemented!");

    const auto& work_buffer = storage_stack[2]->GetData();
    std::memcpy(initial_text.data(), work_buffer.data() + config.initial_string_offset,
                config.initial_string_size);
}

IStorage SoftwareKeyboard::Execute() {
    const auto frontend{GetSoftwareKeyboard()};
    ASSERT(frontend != nullptr);

    const auto parameters = ConvertToFrontendParameters(config, initial_text);

    std::u16string text;
    const auto success = frontend->GetText(parameters, text);

    std::vector<u8> output(SWKBD_OUTPUT_BUFFER_SIZE);

    if (success) {
        output[0] = 1;
        std::memcpy(output.data() + 4, text.data(),
                    std::min<std::size_t>(text.size() * 2, SWKBD_OUTPUT_BUFFER_SIZE - 4));
    }

    return IStorage{output};
}
} // namespace Service::AM::Applets
