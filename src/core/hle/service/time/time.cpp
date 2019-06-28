// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <ctime>
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/service/time/interface.h"
#include "core/hle/service/time/time.h"
#include "core/hle/service/time/time_sharedmemory.h"
#include "core/settings.h"

namespace Service::Time {

static std::chrono::seconds GetSecondsSinceEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()) +
           Settings::values.custom_rtc_differential;
}

static void PosixToCalendar(u64 posix_time, CalendarTime& calendar_time,
                            CalendarAdditionalInfo& additional_info,
                            [[maybe_unused]] const TimeZoneRule& /*rule*/) {
    const std::time_t time(posix_time);
    const std::tm* tm = std::localtime(&time);
    if (tm == nullptr) {
        calendar_time = {};
        additional_info = {};
        return;
    }
    calendar_time.year = tm->tm_year + 1900;
    calendar_time.month = tm->tm_mon + 1;
    calendar_time.day = tm->tm_mday;
    calendar_time.hour = tm->tm_hour;
    calendar_time.minute = tm->tm_min;
    calendar_time.second = tm->tm_sec;

    additional_info.day_of_week = tm->tm_wday;
    additional_info.day_of_year = tm->tm_yday;
    std::memcpy(additional_info.name.data(), "UTC", sizeof("UTC"));
    additional_info.utc_offset = 0;
}

static u64 CalendarToPosix(const CalendarTime& calendar_time,
                           [[maybe_unused]] const TimeZoneRule& /*rule*/) {
    std::tm time{};
    time.tm_year = calendar_time.year - 1900;
    time.tm_mon = calendar_time.month - 1;
    time.tm_mday = calendar_time.day;

    time.tm_hour = calendar_time.hour;
    time.tm_min = calendar_time.minute;
    time.tm_sec = calendar_time.second;

    std::time_t epoch_time = std::mktime(&time);
    return static_cast<u64>(epoch_time);
}

enum class ClockContextType {
    StandardSteady,
    StandardUserSystem,
    StandardNetworkSystem,
    StandardLocalSystem,
};

class ISystemClock final : public ServiceFramework<ISystemClock> {
public:
    ISystemClock(std::shared_ptr<Service::Time::SharedMemory> shared_memory,
                 ClockContextType clock_type)
        : ServiceFramework("ISystemClock"), shared_memory(shared_memory), clock_type(clock_type) {
        static const FunctionInfo functions[] = {
            {0, &ISystemClock::GetCurrentTime, "GetCurrentTime"},
            {1, nullptr, "SetCurrentTime"},
            {2, &ISystemClock::GetSystemClockContext, "GetSystemClockContext"},
            {3, nullptr, "SetSystemClockContext"},

        };
        RegisterHandlers(functions);

        UpdateSharedMemoryContext(system_clock_context);
    }

private:
    void GetCurrentTime(Kernel::HLERequestContext& ctx) {
        const s64 time_since_epoch{GetSecondsSinceEpoch().count()};
        LOG_DEBUG(Service_Time, "called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(time_since_epoch);
    }

    void GetSystemClockContext(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Time, "(STUBBED) called");

        // TODO(ogniK): This should be updated periodically however since we have it stubbed we'll
        // only update when we get a new context
        UpdateSharedMemoryContext(system_clock_context);

        IPC::ResponseBuilder rb{ctx, (sizeof(SystemClockContext) / 4) + 2};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(system_clock_context);
    }

    void UpdateSharedMemoryContext(const SystemClockContext& clock_context) {
        switch (clock_type) {
        case ClockContextType::StandardLocalSystem:
            shared_memory->SetStandardLocalSystemClockContext(clock_context);
            break;
        case ClockContextType::StandardNetworkSystem:
            shared_memory->SetStandardNetworkSystemClockContext(clock_context);
            break;
        }
    }

    SystemClockContext system_clock_context{};
    std::shared_ptr<Service::Time::SharedMemory> shared_memory;
    ClockContextType clock_type;
};

class ISteadyClock final : public ServiceFramework<ISteadyClock> {
public:
    ISteadyClock(std::shared_ptr<SharedMemory> shared_memory)
        : ServiceFramework("ISteadyClock"), shared_memory(shared_memory) {
        static const FunctionInfo functions[] = {
            {0, &ISteadyClock::GetCurrentTimePoint, "GetCurrentTimePoint"},
        };
        RegisterHandlers(functions);

        shared_memory->SetStandardSteadyClockTimepoint(GetCurrentTimePoint());
    }

private:
    void GetCurrentTimePoint(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Time, "called");

        const auto time_point = GetCurrentTimePoint();
        // TODO(ogniK): This should be updated periodically
        shared_memory->SetStandardSteadyClockTimepoint(time_point);

        IPC::ResponseBuilder rb{ctx, (sizeof(SteadyClockTimePoint) / 4) + 2};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(time_point);
    }

    SteadyClockTimePoint GetCurrentTimePoint() const {
        const auto& core_timing = Core::System::GetInstance().CoreTiming();
        const auto ms = Core::Timing::CyclesToMs(core_timing.GetTicks());
        return {static_cast<u64_le>(ms.count() / 1000), {}};
    }

    std::shared_ptr<SharedMemory> shared_memory;
};

class ITimeZoneService final : public ServiceFramework<ITimeZoneService> {
public:
    ITimeZoneService() : ServiceFramework("ITimeZoneService") {
        static const FunctionInfo functions[] = {
            {0, &ITimeZoneService::GetDeviceLocationName, "GetDeviceLocationName"},
            {1, nullptr, "SetDeviceLocationName"},
            {2, &ITimeZoneService::GetTotalLocationNameCount, "GetTotalLocationNameCount"},
            {3, nullptr, "LoadLocationNameList"},
            {4, &ITimeZoneService::LoadTimeZoneRule, "LoadTimeZoneRule"},
            {5, nullptr, "GetTimeZoneRuleVersion"},
            {100, &ITimeZoneService::ToCalendarTime, "ToCalendarTime"},
            {101, &ITimeZoneService::ToCalendarTimeWithMyRule, "ToCalendarTimeWithMyRule"},
            {201, &ITimeZoneService::ToPosixTime, "ToPosixTime"},
            {202, &ITimeZoneService::ToPosixTimeWithMyRule, "ToPosixTimeWithMyRule"},
        };
        RegisterHandlers(functions);
    }

private:
    LocationName location_name{"UTC"};
    TimeZoneRule my_time_zone_rule{};

    void GetDeviceLocationName(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_Time, "called");

        IPC::ResponseBuilder rb{ctx, (sizeof(LocationName) / 4) + 2};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(location_name);
    }

    void GetTotalLocationNameCount(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Time, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(0);
    }

    void LoadTimeZoneRule(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Time, "(STUBBED) called");

        ctx.WriteBuffer(&my_time_zone_rule, sizeof(TimeZoneRule));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void ToCalendarTime(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 posix_time = rp.Pop<u64>();
        LOG_WARNING(Service_Time, "(STUBBED) called, posix_time=0x{:016X}", posix_time);

        TimeZoneRule time_zone_rule{};
        auto buffer = ctx.ReadBuffer();
        std::memcpy(&time_zone_rule, buffer.data(), buffer.size());

        CalendarTime calendar_time{2018, 1, 1, 0, 0, 0};
        CalendarAdditionalInfo additional_info{};

        PosixToCalendar(posix_time, calendar_time, additional_info, time_zone_rule);

        IPC::ResponseBuilder rb{ctx, 10};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(calendar_time);
        rb.PushRaw(additional_info);
    }

    void ToCalendarTimeWithMyRule(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 posix_time = rp.Pop<u64>();
        LOG_WARNING(Service_Time, "(STUBBED) called, posix_time=0x{:016X}", posix_time);

        CalendarTime calendar_time{2018, 1, 1, 0, 0, 0};
        CalendarAdditionalInfo additional_info{};

        PosixToCalendar(posix_time, calendar_time, additional_info, my_time_zone_rule);

        IPC::ResponseBuilder rb{ctx, 10};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw(calendar_time);
        rb.PushRaw(additional_info);
    }

    void ToPosixTime(Kernel::HLERequestContext& ctx) {
        // TODO(ogniK): Figure out how to handle multiple times
        LOG_WARNING(Service_Time, "(STUBBED) called");

        IPC::RequestParser rp{ctx};
        auto calendar_time = rp.PopRaw<CalendarTime>();
        auto posix_time = CalendarToPosix(calendar_time, {});

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u32>(1); // Amount of times we're returning
        ctx.WriteBuffer(&posix_time, sizeof(u64));
    }

    void ToPosixTimeWithMyRule(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_Time, "(STUBBED) called");

        IPC::RequestParser rp{ctx};
        auto calendar_time = rp.PopRaw<CalendarTime>();
        auto posix_time = CalendarToPosix(calendar_time, {});

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.PushRaw<u32>(1); // Amount of times we're returning
        ctx.WriteBuffer(&posix_time, sizeof(u64));
    }
};

void Module::Interface::GetStandardUserSystemClock(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISystemClock>(shared_memory, ClockContextType::StandardUserSystem);
}

void Module::Interface::GetStandardNetworkSystemClock(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISystemClock>(shared_memory, ClockContextType::StandardNetworkSystem);
}

void Module::Interface::GetStandardSteadyClock(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISteadyClock>(shared_memory);
}

void Module::Interface::GetTimeZoneService(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ITimeZoneService>();
}

void Module::Interface::GetStandardLocalSystemClock(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISystemClock>(shared_memory, ClockContextType::StandardLocalSystem);
}

void Module::Interface::GetClockSnapshot(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::RequestParser rp{ctx};
    const auto initial_type = rp.PopRaw<u8>();

    const s64 time_since_epoch{GetSecondsSinceEpoch().count()};
    const std::time_t time(time_since_epoch);
    const std::tm* tm = std::localtime(&time);
    if (tm == nullptr) {
        LOG_ERROR(Service_Time, "tm is a nullptr");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1)); // TODO(ogniK): Find appropriate error code
        return;
    }

    const auto& core_timing = Core::System::GetInstance().CoreTiming();
    const auto ms = Core::Timing::CyclesToMs(core_timing.GetTicks());
    const SteadyClockTimePoint steady_clock_time_point{static_cast<u64_le>(ms.count() / 1000), {}};

    CalendarTime calendar_time{};
    calendar_time.year = tm->tm_year + 1900;
    calendar_time.month = tm->tm_mon + 1;
    calendar_time.day = tm->tm_mday;
    calendar_time.hour = tm->tm_hour;
    calendar_time.minute = tm->tm_min;
    calendar_time.second = tm->tm_sec;

    ClockSnapshot clock_snapshot{};
    clock_snapshot.system_posix_time = time_since_epoch;
    clock_snapshot.network_posix_time = time_since_epoch;
    clock_snapshot.system_calendar_time = calendar_time;
    clock_snapshot.network_calendar_time = calendar_time;

    CalendarAdditionalInfo additional_info{};
    PosixToCalendar(time_since_epoch, calendar_time, additional_info, {});

    clock_snapshot.system_calendar_info = additional_info;
    clock_snapshot.network_calendar_info = additional_info;

    clock_snapshot.steady_clock_timepoint = steady_clock_time_point;
    clock_snapshot.location_name = LocationName{"UTC"};
    clock_snapshot.clock_auto_adjustment_enabled = 1;
    clock_snapshot.type = initial_type;

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
    ctx.WriteBuffer(&clock_snapshot, sizeof(ClockSnapshot));
}

void Module::Interface::CalculateStandardUserSystemClockDifferenceByUser(
    Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");

    IPC::RequestParser rp{ctx};
    const auto snapshot_a = rp.PopRaw<ClockSnapshot>();
    const auto snapshot_b = rp.PopRaw<ClockSnapshot>();
    const u64 difference =
        snapshot_b.user_clock_context.offset - snapshot_a.user_clock_context.offset;

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<u64>(difference);
}

void Module::Interface::GetSharedMemoryNativeHandle(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_Time, "called");
    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(shared_memory->GetSharedMemoryHolder());
}

void Module::Interface::IsStandardUserSystemClockAutomaticCorrectionEnabled(
    Kernel::HLERequestContext& ctx) {
    // ogniK(TODO): When clock contexts are implemented, the value should be read from the context
    // instead of our shared memory holder
    LOG_DEBUG(Service_Time, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u8>(shared_memory->GetStandardUserSystemClockAutomaticCorrectionEnabled());
}

void Module::Interface::SetStandardUserSystemClockAutomaticCorrectionEnabled(
    Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto enabled = rp.Pop<u8>();

    LOG_WARNING(Service_Time, "(PARTIAL IMPLEMENTATION) called");

    // TODO(ogniK): Update clock contexts and correct timespans

    shared_memory->SetStandardUserSystemClockAutomaticCorrectionEnabled(enabled > 0);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

Module::Interface::Interface(std::shared_ptr<Module> time,
                             std::shared_ptr<SharedMemory> shared_memory, const char* name)
    : ServiceFramework(name), time(std::move(time)), shared_memory(std::move(shared_memory)) {}

Module::Interface::~Interface() = default;

void InstallInterfaces(Core::System& system) {
    auto time = std::make_shared<Module>();
    auto shared_mem = std::make_shared<SharedMemory>(system);

    std::make_shared<Time>(time, shared_mem, "time:a")->InstallAsService(system.ServiceManager());
    std::make_shared<Time>(time, shared_mem, "time:s")->InstallAsService(system.ServiceManager());
    std::make_shared<Time>(std::move(time), shared_mem, "time:u")
        ->InstallAsService(system.ServiceManager());
}

} // namespace Service::Time
