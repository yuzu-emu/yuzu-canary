// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <ostream>
#include <vector>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace Core {
class Reporter;
}

namespace Service::LM {

enum class DestinationFlag : u32 {
    Default = 1,
    UART = 2,
    UARTSleeping = 4,

    All = 0xFFFF,
};

struct MessageHeader {
    enum Flags : u32_le {
        IsHead = 1,
        IsTail = 2,
    };
    enum Severity : u32_le {
        Trace,
        Info,
        Warning,
        Error,
        Critical,
    };

    u64_le pid;
    u64_le thread_context;
    union {
        BitField<0, 16, Flags> flags;
        BitField<16, 8, Severity> severity;
        BitField<24, 8, u32> verbosity;
    };
    u32_le payload_size;

    bool IsHeadLog() const {
        return flags & IsHead;
    }
    bool IsTailLog() const {
        return flags & IsTail;
    }
};
static_assert(sizeof(MessageHeader) == 0x18, "MessageHeader is incorrect size");

enum class Field : u8 {
    Skip = 1,
    Message = 2,
    Line = 3,
    Filename = 4,
    Function = 5,
    Module = 6,
    Thread = 7,
};

std::ostream& operator<<(std::ostream& os, DestinationFlag dest);
std::ostream& operator<<(std::ostream& os, MessageHeader::Severity severity);
std::ostream& operator<<(std::ostream& os, Field field);

using FieldMap = std::map<Field, std::vector<u8>>;

struct LogMessage {
    MessageHeader header;
    FieldMap fields;
};

std::string FormatField(Field type, const std::vector<u8>& data);

class Manager {
public:
    explicit Manager(Core::Reporter& reporter);
    ~Manager();

    void SetEnabled(bool enabled);
    void SetDestination(DestinationFlag destination);

    void Log(LogMessage message);

    void Flush();

private:
    void InitializeLog();
    void FinalizeLog();

    bool enabled = true;
    DestinationFlag destination = DestinationFlag::All;

    std::vector<LogMessage> current_log;

    Core::Reporter& reporter;
};

} // namespace Service::LM
