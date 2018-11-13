// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/assert.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/software_keyboard.h"

namespace Service::AM::Applets {

constexpr std::size_t SWKBD_OUTPUT_BUFFER_SIZE = 0x7D8;
constexpr std::size_t SWKBD_OUTPUT_INTERACTIVE_BUFFER_SIZE = 0x7D4;
constexpr std::size_t DEFAULT_MAX_LENGTH = 500;
constexpr bool INTERACTIVE_STATUS_OK = false;

static Core::Frontend::SoftwareKeyboardParameters ConvertToFrontendParameters(
    KeyboardConfig config, std::u16string initial_text) {
    Core::Frontend::SoftwareKeyboardParameters params{};

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

SoftwareKeyboard::SoftwareKeyboard() = default;

SoftwareKeyboard::~SoftwareKeyboard() = default;

void SoftwareKeyboard::Initialize(std::vector<std::shared_ptr<IStorage>> storage_) {
    complete = false;
    initial_text.clear();
    final_data.clear();

    Applet::Initialize(std::move(storage_));

    ASSERT(storage_stack.size() >= 2);
    const auto& keyboard_config = storage_stack[1]->GetData();
    ASSERT(keyboard_config.size() >= sizeof(KeyboardConfig));
    std::memcpy(&config, keyboard_config.data(), sizeof(KeyboardConfig));

    const auto& work_buffer = storage_stack[2]->GetData();

    if (config.initial_string_size == 0)
        return;

    std::vector<char16_t> string(config.initial_string_size);
    std::memcpy(string.data(), work_buffer.data() + 4, string.size() * 2);
    initial_text = Common::UTF16StringFromFixedZeroTerminatedBuffer(string.data(), string.size());
}

bool SoftwareKeyboard::TransactionComplete() const {
    return complete;
}

ResultCode SoftwareKeyboard::GetStatus() const {
    return RESULT_SUCCESS;
}

void SoftwareKeyboard::ReceiveInteractiveData(std::shared_ptr<IStorage> storage) {
    if (complete)
        return;

    const auto data = storage->GetData();
    const auto status = static_cast<bool>(data[0]);

    if (status == INTERACTIVE_STATUS_OK) {
        complete = true;
    } else {
        const auto& frontend{Core::System::GetInstance().GetSoftwareKeyboard()};

        std::array<char16_t, SWKBD_OUTPUT_INTERACTIVE_BUFFER_SIZE / 2 - 2> string;
        std::memcpy(string.data(), data.data() + 4, string.size() * 2);
        frontend.SendTextCheckDialog(
            Common::UTF16StringFromFixedZeroTerminatedBuffer(string.data(), string.size()));
    }
}

void SoftwareKeyboard::Execute(AppletStorageProxyFunction out_data,
                               AppletStorageProxyFunction out_interactive_data) {
    if (complete)
        return;

    const auto& frontend{Core::System::GetInstance().GetSoftwareKeyboard()};

    const auto parameters = ConvertToFrontendParameters(config, initial_text);

    this->out_data = out_data;
    this->out_interactive_data = out_interactive_data;
    frontend.RequestText([this](std::optional<std::u16string> text) { WriteText(text); },
                         parameters);
}

void SoftwareKeyboard::WriteText(std::optional<std::u16string> text) {
    std::vector<u8> output(SWKBD_OUTPUT_BUFFER_SIZE);

    if (text.has_value()) {
        if (config.text_check) {
            const auto size = static_cast<u32>(text->size() * 2 + 4);
            std::memcpy(output.data(), &size, sizeof(u32));
        } else {
            output[0] = 1;
        }

        std::memcpy(output.data() + 4, text->data(),
                    std::min(text->size() * 2, SWKBD_OUTPUT_BUFFER_SIZE - 4));
    } else {
        complete = true;
        out_data(IStorage{output});
        return;
    }

    complete = !config.text_check;

    (complete ? out_data : out_interactive_data)(IStorage{output});
}
} // namespace Service::AM::Applets
