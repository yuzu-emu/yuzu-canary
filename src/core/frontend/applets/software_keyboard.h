// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/bit_field.h"
#include "common/common_types.h"

namespace Frontend {
class SoftwareKeyboardApplet {
public:
    struct Parameters {
        std::u16string submit_text;
        std::u16string header_text;
        std::u16string sub_text;
        std::u16string guide_text;
        std::u16string initial_text;
        std::size_t max_length;
        bool password;
        bool cursor_at_beginning;

        union {
            u8 value;

            BitField<1, 1, u8> disable_space;
            BitField<2, 1, u8> disable_address;
            BitField<3, 1, u8> disable_percent;
            BitField<4, 1, u8> disable_slash;
            BitField<6, 1, u8> disable_number;
            BitField<7, 1, u8> disable_download_code;
        };
    };

    virtual bool GetText(Parameters parameters, std::u16string& text) = 0;
    virtual ~SoftwareKeyboardApplet() = default;
};

class DefaultSoftwareKeyboardApplet final : public SoftwareKeyboardApplet {
    bool GetText(Parameters parameters, std::u16string& text) override;
};

} // namespace Frontend
