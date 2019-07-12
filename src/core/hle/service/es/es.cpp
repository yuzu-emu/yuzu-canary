// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/crypto/key_manager.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/service.h"

namespace Service::ES {

constexpr ResultCode ERROR_INVALID_ARGUMENT{ErrorModule::ETicket, 2};
constexpr ResultCode ERROR_INVALID_RIGHTS_ID{ErrorModule::ETicket, 3};

class ETicket final : public ServiceFramework<ETicket> {
public:
    explicit ETicket() : ServiceFramework{"es"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &ETicket::ImportTicket, "ImportTicket"},
            {2, nullptr, "ImportTicketCertificateSet"},
            {3, nullptr, "DeleteTicket"},
            {4, nullptr, "DeletePersonalizedTicket"},
            {5, nullptr, "DeleteAllCommonTicket"},
            {6, nullptr, "DeleteAllPersonalizedTicket"},
            {7, nullptr, "DeleteAllPersonalizedTicketEx"},
            {8, &ETicket::GetTitleKey, "GetTitleKey"},
            {9, &ETicket::CountCommonTicket, "CountCommonTicket"},
            {10, &ETicket::CountPersonalizedTicket, "CountPersonalizedTicket"},
            {11, &ETicket::ListCommonTicket, "ListCommonTicket"},
            {12, &ETicket::ListPersonalizedTicket, "ListPersonalizedTicket"},
            {13, nullptr, "ListMissingPersonalizedTicket"},
            {14, &ETicket::GetCommonTicketSize, "GetCommonTicketSize"},
            {15, &ETicket::GetPersonalizedTicketSize, "GetPersonalizedTicketSize"},
            {16, &ETicket::GetCommonTicketData, "GetCommonTicketData"},
            {17, &ETicket::GetPersonalizedTicketData, "GetPersonalizedTicketData"},
            {18, nullptr, "OwnTicket"},
            {19, nullptr, "GetTicketInfo"},
            {20, nullptr, "ListLightTicketInfo"},
            {21, nullptr, "SignData"},
            {22, nullptr, "GetCommonTicketAndCertificateSize"},
            {23, nullptr, "GetCommonTicketAndCertificateData"},
            {24, nullptr, "ImportPrepurchaseRecord"},
            {25, nullptr, "DeletePrepurchaseRecord"},
            {26, nullptr, "DeleteAllPrepurchaseRecord"},
            {27, nullptr, "CountPrepurchaseRecord"},
            {28, nullptr, "ListPrepurchaseRecordRightsIds"},
            {29, nullptr, "ListPrepurchaseRecordInfo"},
            {30, nullptr, "CountTicket"},
            {31, nullptr, "ListTicketRightsIds"},
            {32, nullptr, "CountPrepurchaseRecordEx"},
            {33, nullptr, "ListPrepurchaseRecordRightsIdsEx"},
            {34, nullptr, "GetEncryptedTicketSize"},
            {35, nullptr, "GetEncryptedTicketData"},
            {36, nullptr, "DeleteAllInactiveELicenseRequiredPersonalizedTicket"},
            {503, nullptr, "GetTitleKey"},
        };
        // clang-format on
        RegisterHandlers(functions);

        keys.PopulateTickets();
        keys.SynthesizeTickets();
    }

private:
    bool CheckRightsId(Kernel::HLERequestContext& ctx, const u128& rights_id) {
        if (rights_id == u128{}) {
            LOG_ERROR(Service_ETicket, "The rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return false;
        }

        return true;
    }

    void ImportTicket(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto ticket = ctx.ReadBuffer();
        const auto cert = ctx.ReadBuffer(1);

        if (ticket.size() < sizeof(Core::Crypto::Ticket)) {
            LOG_ERROR(Service_ETicket, "The input buffer is not large enough!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        Core::Crypto::Ticket raw{};
        std::memcpy(&raw, ticket.data(), sizeof(Core::Crypto::Ticket));

        if (!keys.AddTicketPersonalized(raw)) {
            LOG_ERROR(Service_ETicket, "The ticket could not be imported!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void GetTitleKey(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto key =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id[1], rights_id[0]);

        if (key == Core::Crypto::Key128{}) {
            LOG_ERROR(Service_ETicket,
                      "The titlekey doesn't exist in the KeyManager or the rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }

        ctx.WriteBuffer(key.data(), key.size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(RESULT_SUCCESS);
    }

    void CountCommonTicket(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_ETicket, "called");

        const auto count = keys.GetCommonTickets().size();

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(count);
    }

    void CountPersonalizedTicket(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_ETicket, "called");

        const auto count = keys.GetPersonalizedTickets().size();

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(count);
    }

    void ListCommonTicket(Kernel::HLERequestContext& ctx) {
        u32 out_entries;
        if (keys.GetCommonTickets().empty())
            out_entries = 0;
        else
            out_entries = ctx.GetWriteBufferSize() / sizeof(u128);

        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        keys.PopulateTickets();
        const auto tickets = keys.GetCommonTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = std::min<u32>(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(out_entries);
    }

    void ListPersonalizedTicket(Kernel::HLERequestContext& ctx) {
        u32 out_entries;
        if (keys.GetPersonalizedTickets().empty())
            out_entries = 0;
        else
            out_entries = ctx.GetWriteBufferSize() / sizeof(u128);

        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        keys.PopulateTickets();
        const auto tickets = keys.GetPersonalizedTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = std::min<u32>(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u32>(out_entries);
    }

    void GetCommonTicketSize(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetCommonTickets().at(rights_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetPersonalizedTicketSize(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetPersonalizedTickets().at(rights_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetCommonTicketData(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetCommonTickets().at(rights_id);

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        ctx.WriteBuffer(&ticket, write_size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(write_size);
    }

    void GetPersonalizedTicketData(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto ticket = keys.GetPersonalizedTickets().at(rights_id);

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        ctx.WriteBuffer(&ticket, write_size);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(RESULT_SUCCESS);
        rb.Push<u64>(write_size);
    }

    Core::Crypto::KeyManager keys;
};

void InstallInterfaces(SM::ServiceManager& service_manager) {
    std::make_shared<ETicket>()->InstallAsService(service_manager);
}

} // namespace Service::ES
