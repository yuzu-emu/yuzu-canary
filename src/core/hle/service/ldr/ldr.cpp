// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <fmt/format.h>
#include <mbedtls/sha256.h>

#include "common/alignment.h"
#include "common/hex_util.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/service.h"
#include "core/loader/nro.h"

namespace Service::LDR {

namespace ErrCodes {
enum {
    InvalidNRO = 52,
    InvalidNRR = 53,
    MissingNRRHash = 54,
    MaximumNRO = 55,
    MaximumNRR = 56,
    AlreadyLoaded = 57,
    InvalidAlignment = 81,
    InvalidSize = 82,
    InvalidNROAddress = 84,
    InvalidNRRAddress = 85,
    NotInitialized = 87,
};
}

constexpr ResultCode ERROR_INVALID_NRO(ErrorModule::Loader, ErrCodes::InvalidNRO);
constexpr ResultCode ERROR_INVALID_NRR(ErrorModule::Loader, ErrCodes::InvalidNRR);
constexpr ResultCode ERROR_MISSING_NRR_HASH(ErrorModule::Loader, ErrCodes::MissingNRRHash);
constexpr ResultCode ERROR_MAXIMUM_NRO(ErrorModule::Loader, ErrCodes::MaximumNRO);
constexpr ResultCode ERROR_MAXIMUM_NRR(ErrorModule::Loader, ErrCodes::MaximumNRR);
constexpr ResultCode ERROR_ALREADY_LOADED(ErrorModule::Loader, ErrCodes::AlreadyLoaded);
constexpr ResultCode ERROR_INVALID_ALIGNMENT(ErrorModule::Loader, ErrCodes::InvalidAlignment);
constexpr ResultCode ERROR_INVALID_SIZE(ErrorModule::Loader, ErrCodes::InvalidSize);
constexpr ResultCode ERROR_INVALID_NRO_ADDRESS(ErrorModule::Loader, ErrCodes::InvalidNROAddress);
constexpr ResultCode ERROR_INVALID_NRR_ADDRESS(ErrorModule::Loader, ErrCodes::InvalidNRRAddress);
constexpr ResultCode ERROR_NOT_INITIALIZED(ErrorModule::Loader, ErrCodes::NotInitialized);

constexpr u64 MAXIMUM_LOADED_RO = 0x40;

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor() : ServiceFramework{"ldr:dmnt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddProcessToDebugLaunchQueue"},
            {1, nullptr, "ClearDebugLaunchQueue"},
            {2, nullptr, "GetNsoInfos"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager() : ServiceFramework{"ldr:pm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CreateProcess"},
            {1, nullptr, "GetProgramInfo"},
            {2, nullptr, "RegisterTitle"},
            {3, nullptr, "UnregisterTitle"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
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
    explicit RelocatableObject() : ServiceFramework{"ldr:ro"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &RelocatableObject::LoadNro, "LoadNro"},
            {1, &RelocatableObject::UnloadNro, "UnloadNro"},
            {2, &RelocatableObject::LoadNrr, "LoadNrr"},
            {3, &RelocatableObject::UnloadNrr, "UnloadNrr"},
            {4, &RelocatableObject::Initialize, "Initialize"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void LoadNrr(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        rp.Skip(2, false);
        const VAddr nrr_addr{rp.Pop<VAddr>()};
        const u64 nrr_size{rp.Pop<u64>()};

        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        if (nro.size() >= MAXIMUM_LOADED_RO) {
            LOG_ERROR(Service_LDR, "Loading new NRR would exceed the maximum number of loaded NRRs "
                                   "(0x40)! Failing...");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_MAXIMUM_NRR);
            return;
        }

        // NRR Address does not fall on 0x1000 byte boundary
        if (!Common::Is4KBAligned(nrr_addr)) {
            LOG_ERROR(Service_LDR, "NRR Address has invalid alignment (actual {:016X})!", nrr_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        // NRR Size is zero or causes overflow
        if (nrr_addr + nrr_size <= nrr_addr || nrr_size == 0 || !Common::Is4KBAligned(nrr_size)) {
            LOG_ERROR(Service_LDR, "NRR Size is invalid! (nrr_address={:016X}, nrr_size={:016X})",
                      nrr_addr, nrr_size);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_SIZE);
            return;
        }
        // Read NRR data from memory
        std::vector<u8> nrr_data(nrr_size);
        Memory::ReadBlock(nrr_addr, nrr_data.data(), nrr_size);
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
        for (std::size_t i = header.hash_offset;
             i < (header.hash_offset + (header.hash_count << 5)); i += 8) {
            hashes.emplace_back();
            std::memcpy(hashes.back().data(), nrr_data.data() + i, sizeof(SHA256Hash));
        }

        nrr.insert_or_assign(nrr_addr, std::move(hashes));

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

        IPC::RequestParser rp{ctx};
        rp.Skip(2, false);
        const auto nrr_addr{rp.Pop<VAddr>()};

        if (!Common::Is4KBAligned(nrr_addr)) {
            LOG_ERROR(Service_LDR, "NRR Address has invalid alignment (actual {:016X})!", nrr_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        const auto iter = nrr.find(nrr_addr);
        if (iter == nrr.end()) {
            LOG_ERROR(Service_LDR,
                      "Attempting to unload NRR which has not been loaded! (addr={:016X})",
                      nrr_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRR_ADDRESS);
            return;
        }

        nrr.erase(iter);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void LoadNro(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        rp.Skip(2, false);
        const VAddr nro_addr{rp.Pop<VAddr>()};
        const u64 nro_size{rp.Pop<u64>()};
        const VAddr bss_addr{rp.Pop<VAddr>()};
        const u64 bss_size{rp.Pop<u64>()};

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
        if (!Common::Is4KBAligned(nro_addr)) {
            LOG_ERROR(Service_LDR, "NRO Address has invalid alignment (actual {:016X})!", nro_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        // NRO Size or BSS Size is zero or causes overflow
        const auto nro_size_valid =
            nro_size != 0 && nro_addr + nro_size > nro_addr && Common::Is4KBAligned(nro_size);
        const auto bss_size_valid = std::numeric_limits<u64>::max() - nro_size >= bss_size &&
                                    (bss_size == 0 || bss_addr + bss_size > bss_addr);

        if (!nro_size_valid || !bss_size_valid) {
            LOG_ERROR(Service_LDR,
                      "NRO Size or BSS Size is invalid! (nro_address={:016X}, nro_size={:016X}, "
                      "bss_address={:016X}, bss_size={:016X})",
                      nro_addr, nro_size, bss_addr, bss_size);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_SIZE);
            return;
        }

        // Read NRO data from memory
        std::vector<u8> nro_data(nro_size);
        Memory::ReadBlock(nro_addr, nro_data.data(), nro_size);

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
                      Common::HexArrayToString(hash));
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

        ASSERT(map_address.Succeeded());

        ASSERT(process->MirrorMemory(*map_address, nro_addr, nro_size,
                                     Kernel::MemoryState::ModuleCodeStatic) == RESULT_SUCCESS);
        ASSERT(process->UnmapMemory(nro_addr, 0, nro_size) == RESULT_SUCCESS);

        if (bss_size > 0) {
            ASSERT(process->MirrorMemory(*map_address + nro_size, bss_addr, bss_size,
                                         Kernel::MemoryState::ModuleCodeStatic) == RESULT_SUCCESS);
            ASSERT(process->UnmapMemory(bss_addr, 0, bss_size) == RESULT_SUCCESS);
        }

        vm_manager.ReprotectRange(*map_address, header.text_size,
                                  Kernel::VMAPermission::ReadExecute);
        vm_manager.ReprotectRange(*map_address + header.ro_offset, header.ro_size,
                                  Kernel::VMAPermission::Read);
        vm_manager.ReprotectRange(*map_address + header.rw_offset, header.rw_size,
                                  Kernel::VMAPermission::ReadWrite);

        Core::System::GetInstance().ArmInterface(0).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(1).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(2).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(3).ClearInstructionCache();

        nro.insert_or_assign(*map_address, NROInfo{hash, nro_size + bss_size});

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push(*map_address);
    }

    void UnloadNro(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        rp.Skip(2, false);
        const VAddr mapped_addr{rp.PopRaw<VAddr>()};
        const VAddr heap_addr{rp.PopRaw<VAddr>()};

        if (!initialized) {
            LOG_ERROR(Service_LDR, "LDR:RO not initialized before use!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_NOT_INITIALIZED);
            return;
        }

        if (!Common::Is4KBAligned(mapped_addr) || !Common::Is4KBAligned(heap_addr)) {
            LOG_ERROR(Service_LDR,
                      "NRO/BSS Address has invalid alignment (actual nro_addr={:016X}, "
                      "bss_addr={:016X})!",
                      mapped_addr, heap_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ALIGNMENT);
            return;
        }

        const auto iter = nro.find(mapped_addr);
        if (iter == nro.end()) {
            LOG_ERROR(Service_LDR,
                      "The NRO attempting to unmap was not mapped or has an invalid address "
                      "(actual {:016X})!",
                      mapped_addr);
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_NRO_ADDRESS);
            return;
        }

        auto* process = Core::CurrentProcess();
        auto& vm_manager = process->VMManager();
        const auto& nro_size = iter->second.size;

        ASSERT(process->MirrorMemory(heap_addr, mapped_addr, nro_size,
                                     Kernel::MemoryState::ModuleCodeStatic) == RESULT_SUCCESS);
        ASSERT(process->UnmapMemory(mapped_addr, 0, nro_size) == RESULT_SUCCESS);

        Core::System::GetInstance().ArmInterface(0).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(1).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(2).ClearInstructionCache();
        Core::System::GetInstance().ArmInterface(3).ClearInstructionCache();

        nro.erase(iter);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void Initialize(Kernel::HLERequestContext& ctx) {
        initialized = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
        LOG_WARNING(Service_LDR, "(STUBBED) called");
    }

private:
    using SHA256Hash = std::array<u8, 0x20>;

    struct NROHeader {
        u32_le entrypoint_insn;
        u32_le mod_offset;
        INSERT_PADDING_WORDS(2);
        u32_le magic;
        INSERT_PADDING_WORDS(1);
        u32_le nro_size;
        INSERT_PADDING_WORDS(1);
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
        INSERT_PADDING_BYTES(0x1C);
        u64_le title_id_mask;
        u64_le title_id_pattern;
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
        u64 size;
    };

    bool initialized = false;

    std::map<VAddr, NROInfo> nro;
    std::map<VAddr, std::vector<SHA256Hash>> nrr;

    bool IsValidNROHash(const SHA256Hash& hash) {
        return std::any_of(
            nrr.begin(), nrr.end(), [&hash](const std::pair<VAddr, std::vector<SHA256Hash>>& p) {
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

void InstallInterfaces(SM::ServiceManager& sm) {
    std::make_shared<DebugMonitor>()->InstallAsService(sm);
    std::make_shared<ProcessManager>()->InstallAsService(sm);
    std::make_shared<Shell>()->InstallAsService(sm);
    std::make_shared<RelocatableObject>()->InstallAsService(sm);
}

} // namespace Service::LDR
