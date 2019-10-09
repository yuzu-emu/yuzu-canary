// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hle/service/lm/manager.h"
#include "core/reporter.h"

namespace Service::LM {

std::ostream& operator<<(std::ostream& os, DestinationFlag dest) {
    std::vector<std::string> array;
    const auto check_single_flag = [dest, &array](DestinationFlag check, std::string name) {
        if ((static_cast<u32>(check) & static_cast<u32>(dest)) != 0) {
            array.emplace_back(std::move(name));
        }
    };

    check_single_flag(DestinationFlag::Default, "Default");
    check_single_flag(DestinationFlag::UART, "UART");
    check_single_flag(DestinationFlag::UARTSleeping, "UART (Sleeping)");

    os << "[";
    for (const auto& entry : array) {
        os << entry << ", ";
    }
    return os << "]";
}

std::ostream& operator<<(std::ostream& os, MessageHeader::Severity severity) {
    switch (severity) {
    case MessageHeader::Severity::Trace:
        return os << "Trace";
    case MessageHeader::Severity::Info:
        return os << "Info";
    case MessageHeader::Severity::Warning:
        return os << "Warning";
    case MessageHeader::Severity::Error:
        return os << "Error";
    case MessageHeader::Severity::Critical:
        return os << "Critical";
    default:
        return os << fmt::format("{:08X}", static_cast<u32>(severity));
    }
}

std::ostream& operator<<(std::ostream& os, Field field) {
    switch (field) {
    case Field::Skip:
        return os << "Skip";
    case Field::Message:
        return os << "Message";
    case Field::Line:
        return os << "Line";
    case Field::Filename:
        return os << "Filename";
    case Field::Function:
        return os << "Function";
    case Field::Module:
        return os << "Module";
    case Field::Thread:
        return os << "Thread";
    default:
        return os << fmt::format("{:08X}", static_cast<u32>(field));
    }
}

std::string FormatField(Field type, const std::vector<u8>& data) {
    switch (type) {
    case Field::Skip:
        return "";
    case Field::Line:
        if (data.size() >= sizeof(u32)) {
            u32 line;
            std::memcpy(&line, data.data(), sizeof(u32));
            return fmt::format("{}", line);
        }
        return "[ERROR DECODING LINE NUMBER]";
    case Field::Message:
    case Field::Filename:
    case Field::Function:
    case Field::Module:
    case Field::Thread:
        return Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(data.data()), data.size());
    default:
        UNIMPLEMENTED();
    }
}

Manager::Manager(Core::Reporter& reporter) : reporter(reporter) {}

Manager::~Manager() = default;

void Manager::SetEnabled(bool enabled) {
    this->enabled = enabled;
}

void Manager::SetDestination(DestinationFlag destination) {
    this->destination = destination;
}

void Manager::Log(LogMessage message) {
    if (message.header.IsHeadLog()) {
        InitializeLog();
    }

    current_log.emplace_back(std::move(message));

    if (current_log.back().header.IsTailLog()) {
        FinalizeLog();
    }
}

void Manager::Flush() {
    FinalizeLog();
}

void Manager::InitializeLog() {
    current_log.clear();

    LOG_INFO(Service_LM, "Initialized new log session");
}

void Manager::FinalizeLog() {
    reporter.SaveLogReport(static_cast<u32>(destination), std::move(current_log));

    LOG_INFO(Service_LM, "Finalized current log session");
}

} // namespace Service::LM
