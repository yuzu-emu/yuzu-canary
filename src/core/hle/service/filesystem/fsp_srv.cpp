// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/file_sys/filesystem.h"
#include "core/file_sys/storage.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp_srv.h"

namespace Service {
namespace FileSystem {

class IStorage final : public ServiceFramework<IStorage> {
public:
    IStorage(std::unique_ptr<FileSys::StorageBackend>&& backend)
        : ServiceFramework("IStorage"), backend(std::move(backend)) {
        static const FunctionInfo functions[] = {
            {0, &IStorage::Read, "Read"}, {1, nullptr, "Write"},   {2, nullptr, "Flush"},
            {3, nullptr, "SetSize"},      {4, nullptr, "GetSize"},
        };
        RegisterHandlers(functions);
    }

private:
    std::unique_ptr<FileSys::StorageBackend> backend;

    void Read(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s64 offset = rp.Pop<s64>();
        const s64 length = rp.Pop<s64>();
        const auto& descriptor = ctx.BufferDescriptorB()[0];

        LOG_DEBUG(Service_FS, "called, offset=0x%llx, length=0x%llx", offset, length);

        // Error checking
        ASSERT_MSG(length == descriptor.Size(), "unexpected size difference");
        if (length < 0) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(ErrorModule::FS, ErrorDescription::InvalidLength));
            return;
        }
        if (offset < 0) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultCode(ErrorModule::FS, ErrorDescription::InvalidOffset));
            return;
        }

        // Read the data from the Storage backend
        std::vector<u8> output(length);
        ResultVal<size_t> res = backend->Read(offset, length, output.data());
        if (res.Failed()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(res.Code());
            return;
        }

        // Write the data to memory
        Memory::WriteBlock(descriptor.Address(), output.data(), descriptor.Size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }
};

FSP_SRV::FSP_SRV() : ServiceFramework("fsp-srv") {
    static const FunctionInfo functions[] = {
        {1, &FSP_SRV::Initalize, "Initalize"},
        {18, &FSP_SRV::MountSdCard, "MountSdCard"},
        {200, &FSP_SRV::OpenDataStorageByCurrentProcess, "OpenDataStorageByCurrentProcess"},
        {202, nullptr, "OpenDataStorageByDataId"},
        {203, &FSP_SRV::OpenRomStorage, "OpenRomStorage"},
        {1005, &FSP_SRV::GetGlobalAccessLogMode, "GetGlobalAccessLogMode"},
    };
    RegisterHandlers(functions);
}

void FSP_SRV::TryLoadRomFS() {
    if (romfs) {
        return;
    }
    FileSys::Path unused;
    auto res = OpenFileSystem(Type::RomFS, unused);
    if (res.Succeeded()) {
        romfs = std::move(res.Unwrap());
    }
}

void FSP_SRV::Initalize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void FSP_SRV::MountSdCard(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void FSP_SRV::GetGlobalAccessLogMode(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(5);
}

void FSP_SRV::OpenDataStorageByCurrentProcess(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_FS, "called");

    TryLoadRomFS();
    if (!romfs) {
        // TODO (bunnei): Find the right error code to use here
        LOG_CRITICAL(Service_FS, "no file system interface available!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1));
        return;
    }

    // Attempt to open a StorageBackend interface to the RomFS
    auto storage = romfs->OpenFile({}, {});
    if (storage.Failed()) {
        LOG_CRITICAL(Service_FS, "no storage interface available!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(storage.Code());
        return;
    }

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IStorage>(std::move(storage.Unwrap()));
}

void FSP_SRV::OpenRomStorage(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called, using OpenDataStorageByCurrentProcess");
    OpenDataStorageByCurrentProcess(ctx);
}

} // namespace FileSystem
} // namespace Service
