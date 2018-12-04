// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <numeric>
#include <vector>
#include "common/logging/log.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/service/aoc/aoc_u.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/settings.h"

namespace Service::AOC {

constexpr u64 DLC_BASE_TITLE_ID_MASK = 0xFFFFFFFFFFFFE000;
constexpr u64 DLC_BASE_TO_AOC_ID = 0x1000;

static bool CheckAOCTitleIDMatchesBase(u64 title_id, u64 base) {
    return (title_id & DLC_BASE_TITLE_ID_MASK) == base;
}

static std::vector<u64> AccumulateAOCTitleIDs() {
    std::vector<u64> add_on_content;
    const auto rcu = FileSystem::GetUnionContents();
    const auto list =
        rcu.ListEntriesFilter(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
    std::transform(list.begin(), list.end(), std::back_inserter(add_on_content),
                   [](const FileSys::RegisteredCacheEntry& rce) { return rce.title_id; });
    add_on_content.erase(
        std::remove_if(
            add_on_content.begin(), add_on_content.end(),
            [&rcu](u64 tid) {
                return rcu.GetEntry(tid, FileSys::ContentRecordType::Data)->GetStatus() !=
                       Loader::ResultStatus::Success;
            }),
        add_on_content.end());
    return add_on_content;
}

AOC_U::AOC_U() : ServiceFramework("aoc:u"), add_on_content(AccumulateAOCTitleIDs()) {
    static const FunctionInfo functions[] = {
        {0, nullptr, "CountAddOnContentByApplicationId"},
        {1, nullptr, "ListAddOnContentByApplicationId"},
        {2, &AOC_U::CountAddOnContent, "CountAddOnContent"},
        {3, &AOC_U::ListAddOnContent, "ListAddOnContent"},
        {4, nullptr, "GetAddOnContentBaseIdByApplicationId"},
        {5, &AOC_U::GetAddOnContentBaseId, "GetAddOnContentBaseId"},
        {6, nullptr, "PrepareAddOnContentByApplicationId"},
        {7, &AOC_U::PrepareAddOnContent, "PrepareAddOnContent"},
        {8, &AOC_U::GetAddOnContentListChangedEvent, "GetAddOnContentListChangedEvent"},
    };
    RegisterHandlers(functions);

    auto& kernel = Core::System::GetInstance().Kernel();
    aoc_change_event = Kernel::WritableEvent::CreateEventPair(kernel, Kernel::ResetType::Sticky,
                                                              "GetAddOnContentListChanged:Event");
}

AOC_U::~AOC_U() = default;

void AOC_U::CountAddOnContent(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AOC, "called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);

    const auto current = Core::System::GetInstance().CurrentProcess()->GetTitleID();

    const auto& disabled = Settings::values.disabled_addons[current];
    if (std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end()) {
        rb.Push<u32>(0);
        return;
    }

    rb.Push<u32>(static_cast<u32>(
        std::count_if(add_on_content.begin(), add_on_content.end(),
                      [current](u64 tid) { return CheckAOCTitleIDMatchesBase(tid, current); })));
}

void AOC_U::ListAddOnContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const auto offset = rp.PopRaw<u32>();
    auto count = rp.PopRaw<u32>();
    LOG_DEBUG(Service_AOC, "called with offset={}, count={}", offset, count);

    const auto current = Core::System::GetInstance().CurrentProcess()->GetTitleID();

    std::vector<u32> out;
    for (size_t i = 0; i < add_on_content.size(); ++i) {
        if ((add_on_content[i] & DLC_BASE_TITLE_ID_MASK) == current)
            out.push_back(static_cast<u32>(add_on_content[i] & 0x7FF));
    }

    const auto& disabled = Settings::values.disabled_addons[current];
    if (std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end())
        out = {};

    if (out.size() < offset) {
        IPC::ResponseBuilder rb{ctx, 2};
        // TODO(DarkLordZach): Find the correct error code.
        rb.Push(ResultCode(-1));
        return;
    }

    count = static_cast<u32>(std::min<size_t>(out.size() - offset, count));
    std::rotate(out.begin(), out.begin() + offset, out.end());
    out.resize(count);

    ctx.WriteBuffer(out);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push(count);
}

void AOC_U::GetAddOnContentBaseId(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_AOC, "called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(RESULT_SUCCESS);
    const auto title_id = Core::System::GetInstance().CurrentProcess()->GetTitleID();
    FileSys::PatchManager pm{title_id};

    const auto res = pm.GetControlMetadata();
    if (res.first == nullptr) {
        rb.Push(title_id + DLC_BASE_TO_AOC_ID);
        return;
    }

    rb.Push(res.first->GetDLCBaseTitleId());
}

void AOC_U::PrepareAddOnContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const auto aoc_id = rp.PopRaw<u32>();
    LOG_WARNING(Service_AOC, "(STUBBED) called with aoc_id={:08X}", aoc_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void AOC_U::GetAddOnContentListChangedEvent(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(aoc_change_event.readable);
}

void InstallInterfaces(SM::ServiceManager& service_manager) {
    std::make_shared<AOC_U>()->InstallAsService(service_manager);
}

} // namespace Service::AOC
