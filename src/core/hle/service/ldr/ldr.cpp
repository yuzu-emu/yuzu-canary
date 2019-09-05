// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <fmt/format.h>
#include <mbedtls/sha256.h>

#include "common/alignment.h"
#include "common/hex_util.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/service.h"
#include "core/loader/nro.h"

namespace Service::LDR {

namespace {

constexpr ResultCode ERROR_ALREADY_REGISTERED{ErrorModule::Loader, 7};
constexpr ResultCode ERROR_TITLE_NOT_FOUND{ErrorModule::Loader, 8};

constexpr ResultCode ERROR_INVALID_MEMORY_STATE{ErrorModule::Loader, 51};
constexpr ResultCode ERROR_INVALID_NRO{ErrorModule::Loader, 52};
constexpr ResultCode ERROR_INVALID_NRR{ErrorModule::Loader, 53};
constexpr ResultCode ERROR_MISSING_NRR_HASH{ErrorModule::Loader, 54};
constexpr ResultCode ERROR_MAXIMUM_NRO{ErrorModule::Loader, 55};
constexpr ResultCode ERROR_MAXIMUM_NRR{ErrorModule::Loader, 56};
constexpr ResultCode ERROR_ALREADY_LOADED{ErrorModule::Loader, 57};
constexpr ResultCode ERROR_INVALID_ALIGNMENT{ErrorModule::Loader, 81};
constexpr ResultCode ERROR_INVALID_SIZE{ErrorModule::Loader, 82};
constexpr ResultCode ERROR_INVALID_NRO_ADDRESS{ErrorModule::Loader, 84};
constexpr ResultCode ERROR_INVALID_NRR_ADDRESS{ErrorModule::Loader, 85};
constexpr ResultCode ERROR_NOT_INITIALIZED{ErrorModule::Loader, 87};

constexpr u64 MAXIMUM_LOADED_RO = 0x40;

constexpr u64 NSO_BUILD_ID_OFFSET = 0x40;

enum class ApplicationType : u8 {
    Sysmodule = 0,
    Application = 1,
    Applet = 2,
};

struct NSOInfo {
    std::array<u8, 0x20> build_id;
    VAddr map_address;
    u64 map_size;
};
static_assert(sizeof(NSOInfo) == 0x30, "NSOInfo has incorrect size.");

struct ProgramInfoHeader {
    u8 main_thread_priority;
    u8 default_cpu_id;
    ApplicationType application_type;
    INSERT_PADDING_BYTES(0x1);
    u32_le main_thread_stack_size;
    u64_le title_id;
    u32_le acid_sac_size;
    u32_le aci0_sac_size;
    u32_le acid_fac_size;
    u32_le aci0_fac_size;
};
static_assert(sizeof(ProgramInfoHeader) == 0x20, "ProgramInfoHeader has incorrect size.");

std::vector<NSOInfo> GetNSOInfo(const std::map<VAddr, std::shared_ptr<std::vector<u8>>>& modules) {
    std::vector<NSOInfo> out;

    for (const auto& [base_addr, memory] : modules) {
        out.emplace_back();
        out.back().map_address = base_addr;
        out.back().map_size = memory->size();
        std::memcpy(out.back().build_id.data(), memory->data() + NSO_BUILD_ID_OFFSET,
                    out.back().build_id.size());
    }

    return out;
}

std::vector<u8> GetProgramInfoFromProcess(const Kernel::Process& process) {
    const auto& meta = process.GetProgramMetadata();

    ProgramInfoHeader header{};
    header.main_thread_priority = meta.GetMainThreadPriority();
    header.default_cpu_id = meta.GetMainThreadCore();
    header.application_type = ApplicationType::Application;
    header.main_thread_stack_size = meta.GetMainThreadStackSize();
    header.title_id = meta.GetTitleID();

    const auto acid_file_access = meta.GetACIDFileAccessControl();
    const auto aci0_file_access = meta.GetACI0FileAccessControl();
    const auto acid_service_access = meta.GetACIDServiceAccessControl();
    const auto aci0_service_access = meta.GetACI0ServiceAccessControl();

    header.acid_fac_size = acid_file_access.size();
    header.aci0_fac_size = aci0_file_access.size();
    header.acid_sac_size = acid_service_access.size();
    header.aci0_sac_size = aci0_service_access.size();

    std::vector<u8> out(sizeof(ProgramInfoHeader) + acid_file_access.size() +
                        aci0_file_access.size() + acid_service_access.size() +
                        aci0_service_access.size());

    u64 offset = 0;
    std::memcpy(out.data() + offset, &header, sizeof(ProgramInfoHeader));
    offset += sizeof(ProgramInfoHeader);
    std::memcpy(out.data() + offset, acid_service_access.data(), acid_service_access.size());
    offset += acid_service_access.size();
    std::memcpy(out.data() + offset, aci0_service_access.data(), aci0_service_access.size());
    offset += aci0_service_access.size();
    std::memcpy(out.data() + offset, acid_file_access.data(), acid_file_access.size());
    offset += acid_file_access.size();
    std::memcpy(out.data() + offset, aci0_file_access.data(), aci0_file_access.size());

    return out;
}

} // Anonymous namespace

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor(const std::vector<Kernel::SharedPtr<Kernel::Process>>& process_list)
        : ServiceFramework{"ldr:dmnt"}, process_list(process_list) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddProcessToDebugLaunchQueue"},
            {1, nullptr, "ClearDebugLaunchQueue"},
            {2, &DebugMonitor::GetNsoInfos, "GetNsoInfos"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetNsoInfos(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto process_id = rp.PopRaw<u64>();

        auto write_size = ctx.GetWriteBufferSize() / sizeof(NSOInfo);

        LOG_DEBUG(Service_LDR, "called, process_id={:016X}, write_size={:08X}", process_id,
                  write_size);

        const auto iter = std::find_if(
            process_list.begin(), process_list.end(),
            [process_id](const auto& process) { return process->GetProcessID() == process_id; });

        if (iter == process_list.end()) {
            LOG_ERROR(Service_LDR, "Cannot find process with given process ID!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_TITLE_NOT_FOUND);
            return;
        }

        const auto& modules = (*iter)->GetModules();
        const auto nso_infos = GetNSOInfo(modules);

        write_size = std::min<std::size_t>(write_size, nso_infos.size());

        ctx.WriteBuffer(nso_infos.data(), write_size * sizeof(NSOInfo));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(write_size);
    }

    const std::vector<Kernel::SharedPtr<Kernel::Process>>& process_list;
};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager(const std::vector<Kernel::SharedPtr<Kernel::Process>>& process_list)
        : ServiceFramework{"ldr:pm"}, process_list(process_list) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CreateProcess"},
            {1, &ProcessManager::GetProgramInfo, "GetProgramInfo"},
            {2, &ProcessManager::RegisterTitle, "RegisterTitle"},
            {3, &ProcessManager::UnregisterTitle, "UnregisterTitle"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetProgramInfo(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto title_id = rp.PopRaw<u64>();
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_LDR, "called, title_id={:016X}, storage_id={:02X}", title_id,
                  static_cast<u8>(storage_id));

        const auto iter =
            std::find_if(process_list.begin(), process_list.end(), [title_id](const auto& process) {
                return process->GetTitleID() == title_id;
            });

        if (iter == process_list.end()) {
            LOG_ERROR(Service_LDR, "Cannot find process with given title ID!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_TITLE_NOT_FOUND);
            return;
        }

        const auto info = GetProgramInfoFromProcess(**iter);

        ctx.WriteBuffer(info);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void RegisterTitle(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto title_id = rp.PopRaw<u64>();
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_LDR, "called, title_id={:016X}, storage_id={:02X}", title_id,
                  static_cast<u8>(storage_id));

        const auto iter =
            std::find_if(registry.begin(), registry.end(),
                         [title_id](const auto& kv) { return kv.second.title_id == title_id; });

        if (iter != registry.end()) {
            LOG_ERROR(Service_LDR, "Cannot register title with identical ID!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_ALREADY_REGISTERED);
            return;
        }

        const auto index = next_register_id;
        registry.insert_or_assign(next_register_id++, TitleRegistration{title_id, storage_id});

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(index);
    }

    void UnregisterTitle(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto register_id = rp.PopRaw<u64>();

        LOG_DEBUG(Service_LDR, "called, register_id={:08X}", register_id);

        const auto iter = registry.find(register_id);

        if (iter == registry.end()) {
            LOG_ERROR(Service_LDR, "Cannot unregister nonexistant ID!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_TITLE_NOT_FOUND);
            return;
        }

        registry.erase(iter);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    struct TitleRegistration {
        u64 title_id;
        FileSys::StorageId storage_id;
    };
    u64 next_register_id = 1;
    std::map<u64, TitleRegistration> registry;

    const std::vector<Kernel::SharedPtr<Kernel::Process>>& process_list;
};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell() : ServiceFramework{"ldr:shel"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddProcessToLaunchQueue"},
            {1, nullptr, "ClearLaunchQueue"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class RelocatableObject final : public ServiceFramework<RelocatableObject> {
public:
    explicit RelocatableObject(const char* name) : ServiceFramework{name} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &RelocatableObject::LoadNro, "LoadNro"},
            {1, &RelocatableObject::UnloadNro, "UnloadNro"},
            {2, &RelocatableObject::LoadNrr, "LoadNrr"},
            {3, &RelocatableObject::UnloadNrr, "UnloadNrr"},
            {4, &RelocatableObject::Initialize, "Initialize"},
            {10, nullptr, "LoadNrrEx"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void LoadNrr(Kernel::HLERequestContext& ctx) {
        struct Parameters {
            u64_le process_id;
            u64_le nrr_address;
            u64_le nrr_size;
        };

        IPC::RequestParser rp{ctx};
        const auto [process_id, nrr_address, nrr_size] = rp.PopRaw<Parameters>();

        LOG_DEBUG(Service_LDR,
                  "called with process_id={:016X}, nrr_address={:016X}, nrr_size={:016X}",
                  process_id, nrr_address, nrr_size);

        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        if (nrr.size() >= MAXIMUM_LOADED_RO) {
            LOG_ERROR(Service_LDR, "Loading new NRR would exceed the maximum number of loaded NRRs "
                                   "(0x40)! Failing...");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_MAXIMUM_NRR);
            return;
        }

        // NRR Address does not fall on 0x1000 byte boundary
        if (!Common::Is4KBAligned(nrr_address)) {
            LOG_ERROR(Service_LDR, "NRR Address has invalid alignment (actual {:016X})!",
                      nrr_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        // NRR Size is zero or causes overflow
        if (nrr_address + nrr_size <= nrr_address || nrr_size == 0 ||
            !Common::Is4KBAligned(nrr_size)) {
            LOG_ERROR(Service_LDR, "NRR Size is invalid! (nrr_address={:016X}, nrr_size={:016X})",
                      nrr_address, nrr_size);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_SIZE);
            return;
        }
        // Read NRR data from memory
        std::vector<u8> nrr_data(nrr_size);
        Memory::ReadBlock(nrr_address, nrr_data.data(), nrr_size);
        NRRHeader header;
        std::memcpy(&header, nrr_data.data(), sizeof(NRRHeader));

        if (header.magic != Common::MakeMagic('N', 'R', 'R', '0')) {
            LOG_ERROR(Service_LDR, "NRR did not have magic 'NRR0' (actual {:08X})!", header.magic);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRR);
            return;
        }

        if (header.size != nrr_size) {
            LOG_ERROR(Service_LDR,
                      "NRR header reported size did not match LoadNrr parameter size! "
                      "(header_size={:016X}, loadnrr_size={:016X})",
                      header.size, nrr_size);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_SIZE);
            return;
        }

        if (Core::CurrentProcess()->GetTitleID() != header.title_id) {
            LOG_ERROR(Service_LDR,
                      "Attempting to load NRR with title ID other than current process. (actual "
                      "{:016X})!",
                      header.title_id);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRR);
            return;
        }

        std::vector<SHA256Hash> hashes;

        // Copy all hashes in the NRR (specified by hash count/hash offset) into vector.
        for (std::size_t i = header.hash_offset;
             i < (header.hash_offset + (header.hash_count * sizeof(SHA256Hash))); i += 8) {
            SHA256Hash hash;
            std::memcpy(hash.data(), nrr_data.data() + i, sizeof(SHA256Hash));
            hashes.emplace_back(hash);
        }

        nrr.insert_or_assign(nrr_address, std::move(hashes));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void UnloadNrr(Kernel::HLERequestContext& ctx) {
        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        struct Parameters {
            u64_le process_id;
            u64_le nrr_address;
        };

        IPC::RequestParser rp{ctx};
        const auto [process_id, nrr_address] = rp.PopRaw<Parameters>();

        LOG_DEBUG(Service_LDR, "called with process_id={:016X}, nrr_addr={:016X}", process_id,
                  nrr_address);

        if (!Common::Is4KBAligned(nrr_address)) {
            LOG_ERROR(Service_LDR, "NRR Address has invalid alignment (actual {:016X})!",
                      nrr_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        const auto iter = nrr.find(nrr_address);
        if (iter == nrr.end()) {
            LOG_ERROR(Service_LDR,
                      "Attempting to unload NRR which has not been loaded! (addr={:016X})",
                      nrr_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRR_ADDRESS);
            return;
        }

        nrr.erase(iter);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void LoadNro(Kernel::HLERequestContext& ctx) {
        struct Parameters {
            u64_le process_id;
            u64_le image_address;
            u64_le image_size;
            u64_le bss_address;
            u64_le bss_size;
        };

        IPC::RequestParser rp{ctx};
        const auto [process_id, nro_address, nro_size, bss_address, bss_size] =
            rp.PopRaw<Parameters>();

        LOG_DEBUG(Service_LDR,
                  "called with pid={:016X}, nro_addr={:016X}, nro_size={:016X}, bss_addr={:016X}, "
                  "bss_size={:016X}",
                  process_id, nro_address, nro_size, bss_address, bss_size);

        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        if (nro.size() >= MAXIMUM_LOADED_RO) {
            LOG_ERROR(Service_LDR, "Loading new NRO would exceed the maximum number of loaded NROs "
                                   "(0x40)! Failing...");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_MAXIMUM_NRO);
            return;
        }

        // NRO Address does not fall on 0x1000 byte boundary
        if (!Common::Is4KBAligned(nro_address)) {
            LOG_ERROR(Service_LDR, "NRO Address has invalid alignment (actual {:016X})!",
                      nro_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        // NRO Size or BSS Size is zero or causes overflow
        const auto nro_size_valid =
            nro_size != 0 && nro_address + nro_size > nro_address && Common::Is4KBAligned(nro_size);
        const auto bss_size_valid = nro_size + bss_size >= nro_size &&
                                    (bss_size == 0 || bss_address + bss_size > bss_address);

        if (!nro_size_valid || !bss_size_valid) {
            LOG_ERROR(Service_LDR,
                      "NRO Size or BSS Size is invalid! (nro_address={:016X}, nro_size={:016X}, "
                      "bss_address={:016X}, bss_size={:016X})",
                      nro_address, nro_size, bss_address, bss_size);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_SIZE);
            return;
        }

        // Read NRO data from memory
        std::vector<u8> nro_data(nro_size);
        Memory::ReadBlock(nro_address, nro_data.data(), nro_size);

        SHA256Hash hash{};
        mbedtls_sha256(nro_data.data(), nro_data.size(), hash.data(), 0);

        // NRO Hash is already loaded
        if (std::any_of(nro.begin(), nro.end(), [&hash](const std::pair<VAddr, NROInfo>& info) {
                return info.second.hash == hash;
            })) {
            LOG_ERROR(Service_LDR, "NRO is already loaded!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_ALREADY_LOADED);
            return;
        }

        // NRO Hash is not in any loaded NRR
        if (!IsValidNROHash(hash)) {
            LOG_ERROR(Service_LDR,
                      "NRO hash is not present in any currently loaded NRRs (hash={})!",
                      Common::HexToString(hash));
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_MISSING_NRR_HASH);
            return;
        }

        NROHeader header;
        std::memcpy(&header, nro_data.data(), sizeof(NROHeader));

        if (!IsValidNRO(header, nro_size, bss_size)) {
            LOG_ERROR(Service_LDR, "NRO was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRO);
            return;
        }

        // Load NRO as new executable module
        auto* process = Core::CurrentProcess();
        auto& vm_manager = process->VMManager();
        auto map_address = vm_manager.FindFreeRegion(nro_size + bss_size);

        if (!map_address.Succeeded() ||
            *map_address + nro_size + bss_size > vm_manager.GetAddressSpaceEndAddress()) {

            LOG_ERROR(Service_LDR,
                      "General error while allocation memory or no available memory to allocate!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_MEMORY_STATE);
            return;
        }

        ASSERT(
            vm_manager
                .MirrorMemory(*map_address, nro_address, nro_size, Kernel::MemoryState::ModuleCode)
                .IsSuccess());
        ASSERT(vm_manager.ReprotectRange(nro_address, nro_size, Kernel::VMAPermission::None)
                   .IsSuccess());

        if (bss_size > 0) {
            ASSERT(vm_manager
                       .MirrorMemory(*map_address + nro_size, bss_address, bss_size,
                                     Kernel::MemoryState::ModuleCode)
                       .IsSuccess());
            ASSERT(vm_manager.ReprotectRange(bss_address, bss_size, Kernel::VMAPermission::None)
                       .IsSuccess());
        }

        vm_manager.ReprotectRange(*map_address, header.text_size,
                                  Kernel::VMAPermission::ReadExecute);
        vm_manager.ReprotectRange(*map_address + header.ro_offset, header.ro_size,
                                  Kernel::VMAPermission::Read);
        vm_manager.ReprotectRange(*map_address + header.rw_offset, header.rw_size,
                                  Kernel::VMAPermission::ReadWrite);

        Core::System::GetInstance().InvalidateCpuInstructionCaches();

        nro.insert_or_assign(*map_address,
                             NROInfo{hash, nro_address, nro_size, bss_address, bss_size});

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(*map_address);
    }

    void UnloadNro(Kernel::HLERequestContext& ctx) {
        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        struct Parameters {
            u64_le process_id;
            u64_le nro_address;
        };

        IPC::RequestParser rp{ctx};
        const auto [process_id, nro_address] = rp.PopRaw<Parameters>();
        LOG_DEBUG(Service_LDR, "called with process_id={:016X}, nro_address=0x{:016X}", process_id,
                  nro_address);

        if (!Common::Is4KBAligned(nro_address)) {
            LOG_ERROR(Service_LDR, "NRO address has invalid alignment (nro_address=0x{:016X})",
                      nro_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        const auto iter = nro.find(nro_address);
        if (iter == nro.end()) {
            LOG_ERROR(Service_LDR,
                      "The NRO attempting to be unmapped was not mapped or has an invalid address "
                      "(nro_address=0x{:016X})!",
                      nro_address);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRO_ADDRESS);
            return;
        }

        auto& vm_manager = Core::CurrentProcess()->VMManager();
        const auto& nro_info = iter->second;

        // Unmap the mirrored memory
        ASSERT(
            vm_manager.UnmapRange(nro_address, nro_info.nro_size + nro_info.bss_size).IsSuccess());

        // Reprotect the source memory
        ASSERT(vm_manager
                   .ReprotectRange(nro_info.nro_address, nro_info.nro_size,
                                   Kernel::VMAPermission::ReadWrite)
                   .IsSuccess());
        if (nro_info.bss_size > 0) {
            ASSERT(vm_manager
                       .ReprotectRange(nro_info.bss_address, nro_info.bss_size,
                                       Kernel::VMAPermission::ReadWrite)
                       .IsSuccess());
        }

        Core::System::GetInstance().InvalidateCpuInstructionCaches();

        nro.erase(iter);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void Initialize(Kernel::HLERequestContext& ctx) {
        LOG_WARNING(Service_LDR, "(STUBBED) called");

        initialized = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

private:
    using SHA256Hash = std::array<u8, 0x20>;

    struct NROHeader {
        INSERT_PADDING_WORDS(1);
        u32_le mod_offset;
        INSERT_PADDING_WORDS(2);
        u32_le magic;
        u32_le version;
        u32_le nro_size;
        u32_le flags;
        u32_le text_offset;
        u32_le text_size;
        u32_le ro_offset;
        u32_le ro_size;
        u32_le rw_offset;
        u32_le rw_size;
        u32_le bss_size;
        INSERT_PADDING_WORDS(1);
        std::array<u8, 0x20> build_id;
        INSERT_PADDING_BYTES(0x20);
    };
    static_assert(sizeof(NROHeader) == 0x80, "NROHeader has invalid size.");

    struct NRRHeader {
        u32_le magic;
        INSERT_PADDING_BYTES(12);
        u64_le title_id_mask;
        u64_le title_id_pattern;
        INSERT_PADDING_BYTES(16);
        std::array<u8, 0x100> modulus;
        std::array<u8, 0x100> signature_1;
        std::array<u8, 0x100> signature_2;
        u64_le title_id;
        u32_le size;
        INSERT_PADDING_BYTES(4);
        u32_le hash_offset;
        u32_le hash_count;
        INSERT_PADDING_BYTES(8);
    };
    static_assert(sizeof(NRRHeader) == 0x350, "NRRHeader has incorrect size.");

    struct NROInfo {
        SHA256Hash hash;
        VAddr nro_address;
        u64 nro_size;
        VAddr bss_address;
        u64 bss_size;
    };

    bool initialized = false;

    std::map<VAddr, NROInfo> nro;
    std::map<VAddr, std::vector<SHA256Hash>> nrr;

    bool IsValidNROHash(const SHA256Hash& hash) const {
        return std::any_of(nrr.begin(), nrr.end(), [&hash](const auto& p) {
            return std::find(p.second.begin(), p.second.end(), hash) != p.second.end();
        });
    }

    static bool IsValidNRO(const NROHeader& header, u64 nro_size, u64 bss_size) {
        return header.magic == Common::MakeMagic('N', 'R', 'O', '0') &&
               header.nro_size == nro_size && header.bss_size == bss_size &&
               header.ro_offset == header.text_offset + header.text_size &&
               header.rw_offset == header.ro_offset + header.ro_size &&
               nro_size == header.rw_offset + header.rw_size &&
               Common::Is4KBAligned(header.text_size) && Common::Is4KBAligned(header.ro_size) &&
               Common::Is4KBAligned(header.rw_size);
    }
};

void InstallInterfaces(Core::System& system) {
    std::make_shared<DebugMonitor>(system.Kernel().GetProcessList())
        ->InstallAsService(system.ServiceManager());
    std::make_shared<ProcessManager>(system.Kernel().GetProcessList())
        ->InstallAsService(system.ServiceManager());
    std::make_shared<Shell>()->InstallAsService(system.ServiceManager());
    std::make_shared<RelocatableObject>("ldr:ro")->InstallAsService(system.ServiceManager());
    std::make_shared<RelocatableObject>("ro:1")->InstallAsService(system.ServiceManager());
}

} // namespace Service::LDR
