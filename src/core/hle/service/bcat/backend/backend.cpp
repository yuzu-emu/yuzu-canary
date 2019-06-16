// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/hex_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/lock.h"
#include "core/hle/service/bcat/backend/backend.h"

namespace Service::BCAT {

ProgressServiceBackend::ProgressServiceBackend(std::string event_name) : impl{} {
    auto& kernel{Core::System::GetInstance().Kernel()};
    event = Kernel::WritableEvent::CreateEventPair(
        kernel, Kernel::ResetType::Automatic, "ProgressServiceBackend:UpdateEvent:" + event_name);
}

Kernel::SharedPtr<Kernel::ReadableEvent> ProgressServiceBackend::GetEvent() {
    return event.readable;
}

DeliveryCacheProgressImpl& ProgressServiceBackend::GetImpl() {
    return impl;
}

void ProgressServiceBackend::SetNeedHLELock(bool need) {
    need_hle_lock = need;
}

void ProgressServiceBackend::SetTotalSize(u64 size) {
    impl.total_bytes = size;
    SignalUpdate();
}

void ProgressServiceBackend::StartConnecting() {
    impl.status = DeliveryCacheProgressImpl::Status::Connecting;
    SignalUpdate();
}

void ProgressServiceBackend::StartProcessingDataList() {
    impl.status = DeliveryCacheProgressImpl::Status::ProcessingDataList;
    SignalUpdate();
}

void ProgressServiceBackend::StartDownloadingFile(std::string_view dir_name,
                                                  std::string_view file_name, u64 file_size) {
    impl.status = DeliveryCacheProgressImpl::Status::Downloading;
    impl.current_downloaded_bytes = 0;
    impl.current_total_bytes = file_size;
    std::memcpy(impl.current_directory.data(), dir_name.data(),
                std::min<u64>(dir_name.size(), 0x31ull));
    std::memcpy(impl.current_file.data(), file_name.data(),
                std::min<u64>(file_name.size(), 0x31ull));
    SignalUpdate();
}

void ProgressServiceBackend::UpdateFileProgress(u64 downloaded) {
    impl.current_downloaded_bytes = downloaded;
    SignalUpdate();
}

void ProgressServiceBackend::FinishDownloadingFile() {
    impl.total_downloaded_bytes += impl.current_total_bytes;
    SignalUpdate();
}

void ProgressServiceBackend::CommitDirectory(std::string_view dir_name) {
    impl.status = DeliveryCacheProgressImpl::Status::Committing;
    impl.current_file.fill(0);
    impl.current_downloaded_bytes = 0;
    impl.current_total_bytes = 0;
    std::memcpy(impl.current_directory.data(), dir_name.data(),
                std::min<u64>(dir_name.size(), 0x31ull));
    SignalUpdate();
}

void ProgressServiceBackend::FinishDownload(ResultCode result) {
    impl.total_downloaded_bytes = impl.total_bytes;
    impl.status = DeliveryCacheProgressImpl::Status::Done;
    impl.result = result;
    SignalUpdate();
}

void ProgressServiceBackend::SignalUpdate() const {
    if (need_hle_lock) {
        std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
        event.writable->Signal();
    } else {
        event.writable->Signal();
    }
}

Backend::Backend(DirectoryGetter getter) : dir_getter(std::move(getter)) {}

Backend::~Backend() = default;

NullBackend::NullBackend(const DirectoryGetter& getter) : Backend(std::move(getter)) {}

NullBackend::~NullBackend() = default;

bool NullBackend::Synchronize(TitleIDVersion title, ProgressServiceBackend& progress) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}, build_id={:016X}", title.title_id,
              title.build_id);

    progress.FinishDownload(RESULT_SUCCESS);
    return true;
}

bool NullBackend::SynchronizeDirectory(TitleIDVersion title, std::string name,
                                       ProgressServiceBackend& progress) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}, build_id={:016X}, name={}", title.title_id,
              title.build_id, name);

    progress.FinishDownload(RESULT_SUCCESS);
    return true;
}

bool NullBackend::Clear(u64 title_id) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}");

    return true;
}

void NullBackend::SetPassphrase(u64 title_id, const Passphrase& passphrase) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}, passphrase = {}", title_id,
              Common::HexToString(passphrase));
}

std::optional<std::vector<u8>> NullBackend::GetLaunchParameter(TitleIDVersion title) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}, build_id={:016X}", title.title_id,
              title.build_id);
    return std::nullopt;
}

} // namespace Service::BCAT
