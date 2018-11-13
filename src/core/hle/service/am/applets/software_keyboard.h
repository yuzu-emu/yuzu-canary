// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_funcs.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applets/applets.h"

namespace Service::AM::Applets {

enum class KeysetDisable : u32 {
    Space = 0x02,
    Address = 0x04,
    Percent = 0x08,
    Slashes = 0x10,
    Numbers = 0x40,
    DownloadCode = 0x80,
};

struct KeyboardConfig {
    INSERT_PADDING_BYTES(4);
    std::array<char16_t, 9> submit_text;
    u16_le left_symbol_key;
    u16_le right_symbol_key;
    INSERT_PADDING_BYTES(1);
    KeysetDisable keyset_disable_bitmask;
    u32_le initial_cursor_position;
    std::array<char16_t, 65> header_text;
    std::array<char16_t, 129> sub_text;
    std::array<char16_t, 257> guide_text;
    u32_le length_limit;
    INSERT_PADDING_BYTES(4);
    u32_le is_password;
    INSERT_PADDING_BYTES(6);
    bool draw_background;
    u32_le initial_string_offset;
    u32_le initial_string_size;
    u32_le user_dictionary_offset;
    u32_le user_dictionary_size;
    bool text_check;
    u64_le text_check_callback;
};
static_assert(sizeof(KeyboardConfig) == 0x3E0, "KeyboardConfig has incorrect size.");

class SoftwareKeyboard final : public Applet {
public:
    SoftwareKeyboard();
    ~SoftwareKeyboard() override;

    void Initialize(std::vector<std::shared_ptr<IStorage>> storage) override;

    bool TransactionComplete() const override;
    ResultCode GetStatus() const override;
    void ReceiveInteractiveData(std::shared_ptr<IStorage> storage) override;
    void Execute(AppletStorageProxyFunction out_data,
                 AppletStorageProxyFunction out_interactive_data) override;

    void WriteText(std::optional<std::u16string> text);

private:
    KeyboardConfig config;
    std::u16string initial_text;
    bool complete = false;
    std::vector<u8> final_data;

    AppletStorageProxyFunction out_data;
    AppletStorageProxyFunction out_interactive_data;
};

} // namespace Service::AM::Applets
