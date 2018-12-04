// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/hex_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/directory.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp_srv.h"

namespace Service::FileSystem {

enum class FileSystemType : u8 {
    Invalid0 = 0,
    Invalid1 = 1,
    Logo = 2,
    ContentControl = 3,
    ContentManual = 4,
    ContentMeta = 5,
    ContentData = 6,
    ApplicationPackage = 7,
};

class IStorage final : public ServiceFramework<IStorage> {
public:
    explicit IStorage(FileSys::VirtualFile backend_)
        : ServiceFramework("IStorage"), backend(std::move(backend_)) {
        static const FunctionInfo functions[] = {
            {0, &IStorage::Read, "Read"}, {1, nullptr, "Write"},   {2, nullptr, "Flush"},
            {3, nullptr, "SetSize"},      {4, nullptr, "GetSize"}, {5, nullptr, "OperateRange"},
        };
        RegisterHandlers(functions);
    }

private:
    FileSys::VirtualFile backend;

    void Read(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s64 offset = rp.Pop<s64>();
        const s64 length = rp.Pop<s64>();

        LOG_DEBUG(Service_FS, "called, offset=0x{:X}, length={}", offset, length);

        // Error checking
        if (length < 0) {
            LOG_ERROR(Service_FS, "Length is less than 0, length={}", length);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_SIZE);
            return;
        }
        if (offset < 0) {
            LOG_ERROR(Service_FS, "Offset is less than 0, offset={}", offset);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_OFFSET);
            return;
        }

        // Read the data from the Storage backend
        std::vector<u8> output = backend->ReadBytes(length, offset);
        // Write the data to memory
        ctx.WriteBuffer(output);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }
};

class IFile final : public ServiceFramework<IFile> {
public:
    explicit IFile(FileSys::VirtualFile backend_)
        : ServiceFramework("IFile"), backend(std::move(backend_)) {
        static const FunctionInfo functions[] = {
            {0, &IFile::Read, "Read"},       {1, &IFile::Write, "Write"},
            {2, &IFile::Flush, "Flush"},     {3, &IFile::SetSize, "SetSize"},
            {4, &IFile::GetSize, "GetSize"}, {5, nullptr, "OperateRange"},
        };
        RegisterHandlers(functions);
    }

private:
    FileSys::VirtualFile backend;

    void Read(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 unk = rp.Pop<u64>();
        const s64 offset = rp.Pop<s64>();
        const s64 length = rp.Pop<s64>();

        LOG_DEBUG(Service_FS, "called, offset=0x{:X}, length={}", offset, length);

        // Error checking
        if (length < 0) {
            LOG_ERROR(Service_FS, "Length is less than 0, length={}", length);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_SIZE);
            return;
        }
        if (offset < 0) {
            LOG_ERROR(Service_FS, "Offset is less than 0, offset={}", offset);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_OFFSET);
            return;
        }

        // Read the data from the Storage backend
        std::vector<u8> output = backend->ReadBytes(length, offset);

        // Write the data to memory
        ctx.WriteBuffer(output);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(static_cast<u64>(output.size()));
    }

    void Write(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 unk = rp.Pop<u64>();
        const s64 offset = rp.Pop<s64>();
        const s64 length = rp.Pop<s64>();

        LOG_DEBUG(Service_FS, "called, offset=0x{:X}, length={}", offset, length);

        // Error checking
        if (length < 0) {
            LOG_ERROR(Service_FS, "Length is less than 0, length={}", length);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_SIZE);
            return;
        }
        if (offset < 0) {
            LOG_ERROR(Service_FS, "Offset is less than 0, offset={}", offset);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(FileSys::ERROR_INVALID_OFFSET);
            return;
        }

        const std::vector<u8> data = ctx.ReadBuffer();

        ASSERT_MSG(
            static_cast<s64>(data.size()) <= length,
            "Attempting to write more data than requested (requested={:016X}, actual={:016X}).",
            length, data.size());

        // Write the data to the Storage backend
        const auto write_size =
            static_cast<std::size_t>(std::distance(data.begin(), data.begin() + length));
        const std::size_t written = backend->Write(data.data(), write_size, offset);

        ASSERT_MSG(static_cast<s64>(written) == length,
                   "Could not write all bytes to file (requested={:016X}, actual={:016X}).", length,
                   written);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void Flush(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_FS, "called");

        // Exists for SDK compatibiltity -- No need to flush file.

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void SetSize(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 size = rp.Pop<u64>();
        LOG_DEBUG(Service_FS, "called, size={}", size);

        backend->Resize(size);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetSize(Kernel::HLERequestContext& ctx) {
        const u64 size = backend->GetSize();
        LOG_DEBUG(Service_FS, "called, size={}", size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(size);
    }
};

template <typename T>
static void BuildEntryIndex(std::vector<FileSys::Entry>& entries, const std::vector<T>& new_data,
                            FileSys::EntryType type) {
    entries.reserve(entries.size() + new_data.size());

    for (const auto& new_entry : new_data) {
        entries.emplace_back(new_entry->GetName(), type, new_entry->GetSize());
    }
}

class IDirectory final : public ServiceFramework<IDirectory> {
public:
    explicit IDirectory(FileSys::VirtualDir backend_)
        : ServiceFramework("IDirectory"), backend(std::move(backend_)) {
        static const FunctionInfo functions[] = {
            {0, &IDirectory::Read, "Read"},
            {1, &IDirectory::GetEntryCount, "GetEntryCount"},
        };
        RegisterHandlers(functions);

        // TODO(DarkLordZach): Verify that this is the correct behavior.
        // Build entry index now to save time later.
        BuildEntryIndex(entries, backend->GetFiles(), FileSys::File);
        BuildEntryIndex(entries, backend->GetSubdirectories(), FileSys::Directory);
    }

private:
    FileSys::VirtualDir backend;
    std::vector<FileSys::Entry> entries;
    u64 next_entry_index = 0;

    void Read(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u64 unk = rp.Pop<u64>();

        LOG_DEBUG(Service_FS, "called, unk=0x{:X}", unk);

        // Calculate how many entries we can fit in the output buffer
        const u64 count_entries = ctx.GetWriteBufferSize() / sizeof(FileSys::Entry);

        // Cap at total number of entries.
        const u64 actual_entries = std::min(count_entries, entries.size() - next_entry_index);

        // Determine data start and end
        const auto* begin = reinterpret_cast<u8*>(entries.data() + next_entry_index);
        const auto* end = reinterpret_cast<u8*>(entries.data() + next_entry_index + actual_entries);
        const auto range_size = static_cast<std::size_t>(std::distance(begin, end));

        next_entry_index += actual_entries;

        // Write the data to memory
        ctx.WriteBuffer(begin, range_size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(actual_entries);
    }

    void GetEntryCount(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_FS, "called");

        u64 count = entries.size() - next_entry_index;

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(count);
    }
};

class IFileSystem final : public ServiceFramework<IFileSystem> {
public:
    explicit IFileSystem(FileSys::VirtualDir backend)
        : ServiceFramework("IFileSystem"), backend(std::move(backend)) {
        static const FunctionInfo functions[] = {
            {0, &IFileSystem::CreateFile, "CreateFile"},
            {1, &IFileSystem::DeleteFile, "DeleteFile"},
            {2, &IFileSystem::CreateDirectory, "CreateDirectory"},
            {3, &IFileSystem::DeleteDirectory, "DeleteDirectory"},
            {4, &IFileSystem::DeleteDirectoryRecursively, "DeleteDirectoryRecursively"},
            {5, &IFileSystem::RenameFile, "RenameFile"},
            {6, nullptr, "RenameDirectory"},
            {7, &IFileSystem::GetEntryType, "GetEntryType"},
            {8, &IFileSystem::OpenFile, "OpenFile"},
            {9, &IFileSystem::OpenDirectory, "OpenDirectory"},
            {10, &IFileSystem::Commit, "Commit"},
            {11, nullptr, "GetFreeSpaceSize"},
            {12, nullptr, "GetTotalSpaceSize"},
            {13, &IFileSystem::CleanDirectoryRecursively, "CleanDirectoryRecursively"},
            {14, nullptr, "GetFileTimeStampRaw"},
            {15, nullptr, "QueryEntry"},
        };
        RegisterHandlers(functions);
    }

    void CreateFile(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        u64 mode = rp.Pop<u64>();
        u32 size = rp.Pop<u32>();

        LOG_DEBUG(Service_FS, "called file {} mode 0x{:X} size 0x{:08X}", name, mode, size);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.CreateFile(name, size));
    }

    void DeleteFile(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called file {}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.DeleteFile(name));
    }

    void CreateDirectory(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called directory {}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.CreateDirectory(name));
    }

    void DeleteDirectory(Kernel::HLERequestContext& ctx) {
        const IPC::RequestParser rp{ctx};

        const auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called directory {}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.DeleteDirectory(name));
    }

    void DeleteDirectoryRecursively(Kernel::HLERequestContext& ctx) {
        const IPC::RequestParser rp{ctx};

        const auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called directory {}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.DeleteDirectoryRecursively(name));
    }

    void CleanDirectoryRecursively(Kernel::HLERequestContext& ctx) {
        const auto file_buffer = ctx.ReadBuffer();
        const std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called. Directory: {}", name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.CleanDirectoryRecursively(name));
    }

    void RenameFile(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        std::vector<u8> buffer;
        buffer.resize(ctx.BufferDescriptorX()[0].Size());
        Memory::ReadBlock(ctx.BufferDescriptorX()[0].Address(), buffer.data(), buffer.size());
        std::string src_name = Common::StringFromBuffer(buffer);

        buffer.resize(ctx.BufferDescriptorX()[1].Size());
        Memory::ReadBlock(ctx.BufferDescriptorX()[1].Address(), buffer.data(), buffer.size());
        std::string dst_name = Common::StringFromBuffer(buffer);

        LOG_DEBUG(Service_FS, "called file '{}' to file '{}'", src_name, dst_name);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(backend.RenameFile(src_name, dst_name));
    }

    void OpenFile(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        auto mode = static_cast<FileSys::Mode>(rp.Pop<u32>());

        LOG_DEBUG(Service_FS, "called file {} mode {}", name, static_cast<u32>(mode));

        auto result = backend.OpenFile(name, mode);
        if (result.Failed()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result.Code());
            return;
        }

        IFile file(result.Unwrap());

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IFile>(std::move(file));
    }

    void OpenDirectory(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        // TODO(Subv): Implement this filter.
        u32 filter_flags = rp.Pop<u32>();

        LOG_DEBUG(Service_FS, "called directory {} filter {}", name, filter_flags);

        auto result = backend.OpenDirectory(name);
        if (result.Failed()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result.Code());
            return;
        }

        IDirectory directory(result.Unwrap());

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<IDirectory>(std::move(directory));
    }

    void GetEntryType(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};

        auto file_buffer = ctx.ReadBuffer();
        std::string name = Common::StringFromBuffer(file_buffer);

        LOG_DEBUG(Service_FS, "called file {}", name);

        auto result = backend.GetEntryType(name);
        if (result.Failed()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result.Code());
            return;
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(*result));
    }

    void Commit(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_FS, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

private:
    VfsDirectoryServiceWrapper backend;
};

class ISaveDataInfoReader final : public ServiceFramework<ISaveDataInfoReader> {
public:
    explicit ISaveDataInfoReader(FileSys::SaveDataSpaceId space)
        : ServiceFramework("ISaveDataInfoReader") {
        static const FunctionInfo functions[] = {
            {0, &ISaveDataInfoReader::ReadSaveDataInfo, "ReadSaveDataInfo"},
        };
        RegisterHandlers(functions);

        FindAllSaves(space);
    }

    void ReadSaveDataInfo(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_FS, "called");

        // Calculate how many entries we can fit in the output buffer
        const u64 count_entries = ctx.GetWriteBufferSize() / sizeof(SaveDataInfo);

        // Cap at total number of entries.
        const u64 actual_entries = std::min(count_entries, info.size() - next_entry_index);

        // Determine data start and end
        const auto* begin = reinterpret_cast<u8*>(info.data() + next_entry_index);
        const auto* end = reinterpret_cast<u8*>(info.data() + next_entry_index + actual_entries);
        const auto range_size = static_cast<std::size_t>(std::distance(begin, end));

        next_entry_index += actual_entries;

        // Write the data to memory
        ctx.WriteBuffer(begin, range_size);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(static_cast<u32>(actual_entries));
    }

private:
    static u64 stoull_be(std::string_view str) {
        if (str.size() != 16)
            return 0;

        const auto bytes = Common::HexStringToArray<0x8>(str);
        u64 out{};
        std::memcpy(&out, bytes.data(), sizeof(u64));

        return Common::swap64(out);
    }

    void FindAllSaves(FileSys::SaveDataSpaceId space) {
        const auto save_root = OpenSaveDataSpace(space);
        ASSERT(save_root.Succeeded());

        for (const auto& type : (*save_root)->GetSubdirectories()) {
            if (type->GetName() == "save") {
                for (const auto& save_id : type->GetSubdirectories()) {
                    for (const auto& user_id : save_id->GetSubdirectories()) {
                        const auto save_id_numeric = stoull_be(save_id->GetName());
                        auto user_id_numeric = Common::HexStringToArray<0x10>(user_id->GetName());
                        std::reverse(user_id_numeric.begin(), user_id_numeric.end());

                        if (save_id_numeric != 0) {
                            // System Save Data
                            info.emplace_back(SaveDataInfo{
                                0,
                                space,
                                FileSys::SaveDataType::SystemSaveData,
                                {},
                                user_id_numeric,
                                save_id_numeric,
                                0,
                                user_id->GetSize(),
                                {},
                            });

                            continue;
                        }

                        for (const auto& title_id : user_id->GetSubdirectories()) {
                            const auto device =
                                std::all_of(user_id_numeric.begin(), user_id_numeric.end(),
                                            [](u8 val) { return val == 0; });
                            info.emplace_back(SaveDataInfo{
                                0,
                                space,
                                device ? FileSys::SaveDataType::DeviceSaveData
                                       : FileSys::SaveDataType::SaveData,
                                {},
                                user_id_numeric,
                                save_id_numeric,
                                stoull_be(title_id->GetName()),
                                title_id->GetSize(),
                                {},
                            });
                        }
                    }
                }
            } else if (space == FileSys::SaveDataSpaceId::TemporaryStorage) {
                // Temporary Storage
                for (const auto& user_id : type->GetSubdirectories()) {
                    for (const auto& title_id : user_id->GetSubdirectories()) {
                        if (!title_id->GetFiles().empty() ||
                            !title_id->GetSubdirectories().empty()) {
                            auto user_id_numeric =
                                Common::HexStringToArray<0x10>(user_id->GetName());
                            std::reverse(user_id_numeric.begin(), user_id_numeric.end());

                            info.emplace_back(SaveDataInfo{
                                0,
                                space,
                                FileSys::SaveDataType::TemporaryStorage,
                                {},
                                user_id_numeric,
                                stoull_be(type->GetName()),
                                stoull_be(title_id->GetName()),
                                title_id->GetSize(),
                                {},
                            });
                        }
                    }
                }
            }
        }
    }

    struct SaveDataInfo {
        u64_le save_id_unknown;
        FileSys::SaveDataSpaceId space;
        FileSys::SaveDataType type;
        INSERT_PADDING_BYTES(0x6);
        std::array<u8, 0x10> user_id;
        u64_le save_id;
        u64_le title_id;
        u64_le save_image_size;
        INSERT_PADDING_BYTES(0x28);
    };
    static_assert(sizeof(SaveDataInfo) == 0x60, "SaveDataInfo has incorrect size.");

    std::vector<SaveDataInfo> info;
    u64 next_entry_index = 0;
};

FSP_SRV::FSP_SRV() : ServiceFramework("fsp-srv") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "MountContent"},
        {1, &FSP_SRV::Initialize, "Initialize"},
        {2, nullptr, "OpenDataFileSystemByCurrentProcess"},
        {7, &FSP_SRV::OpenFileSystemWithPatch, "OpenFileSystemWithPatch"},
        {8, nullptr, "OpenFileSystemWithId"},
        {9, nullptr, "OpenDataFileSystemByApplicationId"},
        {11, nullptr, "OpenBisFileSystem"},
        {12, nullptr, "OpenBisStorage"},
        {13, nullptr, "InvalidateBisCache"},
        {17, nullptr, "OpenHostFileSystem"},
        {18, &FSP_SRV::MountSdCard, "MountSdCard"},
        {19, nullptr, "FormatSdCardFileSystem"},
        {21, nullptr, "DeleteSaveDataFileSystem"},
        {22, &FSP_SRV::CreateSaveData, "CreateSaveData"},
        {23, nullptr, "CreateSaveDataFileSystemBySystemSaveDataId"},
        {24, nullptr, "RegisterSaveDataFileSystemAtomicDeletion"},
        {25, nullptr, "DeleteSaveDataFileSystemBySaveDataSpaceId"},
        {26, nullptr, "FormatSdCardDryRun"},
        {27, nullptr, "IsExFatSupported"},
        {28, nullptr, "DeleteSaveDataFileSystemBySaveDataAttribute"},
        {30, nullptr, "OpenGameCardStorage"},
        {31, nullptr, "OpenGameCardFileSystem"},
        {32, nullptr, "ExtendSaveDataFileSystem"},
        {33, nullptr, "DeleteCacheStorage"},
        {34, nullptr, "GetCacheStorageSize"},
        {51, &FSP_SRV::MountSaveData, "MountSaveData"},
        {52, nullptr, "OpenSaveDataFileSystemBySystemSaveDataId"},
        {53, &FSP_SRV::OpenReadOnlySaveDataFileSystem, "OpenReadOnlySaveDataFileSystem"},
        {57, nullptr, "ReadSaveDataFileSystemExtraDataBySaveDataSpaceId"},
        {58, nullptr, "ReadSaveDataFileSystemExtraData"},
        {59, nullptr, "WriteSaveDataFileSystemExtraData"},
        {60, nullptr, "OpenSaveDataInfoReader"},
        {61, &FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId, "OpenSaveDataInfoReaderBySaveDataSpaceId"},
        {62, nullptr, "OpenCacheStorageList"},
        {64, nullptr, "OpenSaveDataInternalStorageFileSystem"},
        {65, nullptr, "UpdateSaveDataMacForDebug"},
        {66, nullptr, "WriteSaveDataFileSystemExtraData2"},
        {80, nullptr, "OpenSaveDataMetaFile"},
        {81, nullptr, "OpenSaveDataTransferManager"},
        {82, nullptr, "OpenSaveDataTransferManagerVersion2"},
        {83, nullptr, "OpenSaveDataTransferProhibiterForCloudBackUp"},
        {100, nullptr, "OpenImageDirectoryFileSystem"},
        {110, nullptr, "OpenContentStorageFileSystem"},
        {200, &FSP_SRV::OpenDataStorageByCurrentProcess, "OpenDataStorageByCurrentProcess"},
        {201, nullptr, "OpenDataStorageByProgramId"},
        {202, &FSP_SRV::OpenDataStorageByDataId, "OpenDataStorageByDataId"},
        {203, &FSP_SRV::OpenRomStorage, "OpenRomStorage"},
        {400, nullptr, "OpenDeviceOperator"},
        {500, nullptr, "OpenSdCardDetectionEventNotifier"},
        {501, nullptr, "OpenGameCardDetectionEventNotifier"},
        {510, nullptr, "OpenSystemDataUpdateEventNotifier"},
        {511, nullptr, "NotifySystemDataUpdateEvent"},
        {600, nullptr, "SetCurrentPosixTime"},
        {601, nullptr, "QuerySaveDataTotalSize"},
        {602, nullptr, "VerifySaveDataFileSystem"},
        {603, nullptr, "CorruptSaveDataFileSystem"},
        {604, nullptr, "CreatePaddingFile"},
        {605, nullptr, "DeleteAllPaddingFiles"},
        {606, nullptr, "GetRightsId"},
        {607, nullptr, "RegisterExternalKey"},
        {608, nullptr, "UnregisterAllExternalKey"},
        {609, nullptr, "GetRightsIdByPath"},
        {610, nullptr, "GetRightsIdAndKeyGenerationByPath"},
        {611, nullptr, "SetCurrentPosixTimeWithTimeDifference"},
        {612, nullptr, "GetFreeSpaceSizeForSaveData"},
        {613, nullptr, "VerifySaveDataFileSystemBySaveDataSpaceId"},
        {614, nullptr, "CorruptSaveDataFileSystemBySaveDataSpaceId"},
        {615, nullptr, "QuerySaveDataInternalStorageTotalSize"},
        {616, nullptr, "GetSaveDataCommitId"},
        {620, nullptr, "SetSdCardEncryptionSeed"},
        {630, nullptr, "SetSdCardAccessibility"},
        {631, nullptr, "IsSdCardAccessible"},
        {640, nullptr, "IsSignedSystemPartitionOnSdCardValid"},
        {700, nullptr, "OpenAccessFailureResolver"},
        {701, nullptr, "GetAccessFailureDetectionEvent"},
        {702, nullptr, "IsAccessFailureDetected"},
        {710, nullptr, "ResolveAccessFailure"},
        {720, nullptr, "AbandonAccessFailure"},
        {800, nullptr, "GetAndClearFileSystemProxyErrorInfo"},
        {1000, nullptr, "SetBisRootForHost"},
        {1001, nullptr, "SetSaveDataSize"},
        {1002, nullptr, "SetSaveDataRootPath"},
        {1003, nullptr, "DisableAutoSaveDataCreation"},
        {1004, nullptr, "SetGlobalAccessLogMode"},
        {1005, &FSP_SRV::GetGlobalAccessLogMode, "GetGlobalAccessLogMode"},
        {1006, nullptr, "OutputAccessLogToSdCard"},
        {1007, nullptr, "RegisterUpdatePartition"},
        {1008, nullptr, "OpenRegisteredUpdatePartition"},
        {1009, nullptr, "GetAndClearMemoryReportInfo"},
        {1100, nullptr, "OverrideSaveDataTransferTokenSignVerificationKey"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

FSP_SRV::~FSP_SRV() = default;

void FSP_SRV::Initialize(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void FSP_SRV::OpenFileSystemWithPatch(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    const auto type = rp.PopRaw<FileSystemType>();
    const auto title_id = rp.PopRaw<u64>();
    LOG_WARNING(Service_FS, "(STUBBED) called with type={}, title_id={:016X}",
                static_cast<u8>(type), title_id);

    IPC::ResponseBuilder rb{ctx, 2, 0, 0};
    rb.Push(ResultCode(-1));
}

void FSP_SRV::MountSdCard(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_FS, "called");

    IFileSystem filesystem(OpenSDMC().Unwrap());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IFileSystem>(std::move(filesystem));
}

void FSP_SRV::CreateSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    auto save_struct = rp.PopRaw<FileSys::SaveDataDescriptor>();
    auto save_create_struct = rp.PopRaw<std::array<u8, 0x40>>();
    u128 uid = rp.PopRaw<u128>();

    LOG_WARNING(Service_FS, "(STUBBED) called save_struct = {}, uid = {:016X}{:016X}",
                save_struct.DebugInfo(), uid[1], uid[0]);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(RESULT_SUCCESS);
}

void FSP_SRV::MountSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    auto space_id = rp.PopRaw<FileSys::SaveDataSpaceId>();
    auto unk = rp.Pop<u32>();
    LOG_INFO(Service_FS, "called with unknown={:08X}", unk);

    auto save_struct = rp.PopRaw<FileSys::SaveDataDescriptor>();

    auto dir = OpenSaveData(space_id, save_struct);

    if (dir.Failed()) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 0};
        rb.Push(FileSys::ERROR_ENTITY_NOT_FOUND);
        return;
    }

    IFileSystem filesystem(std::move(dir.Unwrap()));

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IFileSystem>(std::move(filesystem));
}

void FSP_SRV::OpenReadOnlySaveDataFileSystem(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called, delegating to 51 OpenSaveDataFilesystem");
    MountSaveData(ctx);
}

void FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto space = rp.PopRaw<FileSys::SaveDataSpaceId>();
    LOG_INFO(Service_FS, "called, space={}", static_cast<u8>(space));

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<ISaveDataInfoReader>(std::make_shared<ISaveDataInfoReader>(space));
}

void FSP_SRV::GetGlobalAccessLogMode(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(RESULT_SUCCESS);
    rb.Push<u32>(5);
}

void FSP_SRV::OpenDataStorageByCurrentProcess(Kernel::HLERequestContext& ctx) {
    LOG_DEBUG(Service_FS, "called");

    auto romfs = OpenRomFSCurrentProcess();
    if (romfs.Failed()) {
        // TODO (bunnei): Find the right error code to use here
        LOG_CRITICAL(Service_FS, "no file system interface available!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1));
        return;
    }

    IStorage storage(std::move(romfs.Unwrap()));

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IStorage>(std::move(storage));
}

void FSP_SRV::OpenDataStorageByDataId(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto storage_id = rp.PopRaw<FileSys::StorageId>();
    const auto unknown = rp.PopRaw<u32>();
    const auto title_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_FS, "called with storage_id={:02X}, unknown={:08X}, title_id={:016X}",
              static_cast<u8>(storage_id), unknown, title_id);

    auto data = OpenRomFS(title_id, storage_id, FileSys::ContentRecordType::Data);

    if (data.Failed()) {
        const auto archive = FileSys::SystemArchive::SynthesizeSystemArchive(title_id);

        if (archive != nullptr) {
            IPC::ResponseBuilder rb{ctx, 2, 0, 1};
            rb.Push(RESULT_SUCCESS);
            rb.PushIpcInterface(std::make_shared<IStorage>(archive));
            return;
        }

        // TODO(DarkLordZach): Find the right error code to use here
        LOG_ERROR(Service_FS,
                  "could not open data storage with title_id={:016X}, storage_id={:02X}", title_id,
                  static_cast<u8>(storage_id));
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultCode(-1));
        return;
    }

    FileSys::PatchManager pm{title_id};

    IStorage storage(pm.PatchRomFS(std::move(data.Unwrap()), 0, FileSys::ContentRecordType::Data));

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(RESULT_SUCCESS);
    rb.PushIpcInterface<IStorage>(std::move(storage));
}

void FSP_SRV::OpenRomStorage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    auto storage_id = rp.PopRaw<FileSys::StorageId>();
    auto title_id = rp.PopRaw<u64>();

    LOG_DEBUG(Service_FS, "called with storage_id={:02X}, title_id={:016X}",
              static_cast<u8>(storage_id), title_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(FileSys::ERROR_ENTITY_NOT_FOUND);
}

} // namespace Service::FileSystem
