// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <tuple>
#include <utility>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/kernel/thread.h"

namespace Kernel {

ServerSession::ServerSession(KernelCore& kernel) : WaitObject{kernel} {}
ServerSession::~ServerSession() {
    // This destructor will be called automatically when the last ServerSession handle is closed by
    // the emulated application.

    // Decrease the port's connection count.
    if (parent->port) {
        parent->port->ConnectionClosed();
    }

    parent->server = nullptr;
}

ResultVal<SharedPtr<ServerSession>> ServerSession::Create(KernelCore& kernel, std::string name) {
    SharedPtr<ServerSession> server_session(new ServerSession(kernel));

    server_session->name = std::move(name);
    server_session->parent = nullptr;

    return MakeResult(std::move(server_session));
}

bool ServerSession::ShouldWait(const Thread* thread) const {
    // Closed sessions should never wait, an error will be returned from svcReplyAndReceive.
    if (parent->client == nullptr)
        return false;
    // Wait if we have no pending requests, or if we're currently handling a request.
    return pending_requesting_threads.empty() || currently_handling != nullptr;
}

void ServerSession::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");
    // We are now handling a request, pop it from the stack.
    // TODO(Subv): What happens if the client endpoint is closed before any requests are made?
    ASSERT(!pending_requesting_threads.empty());
    currently_handling = pending_requesting_threads.back();
    pending_requesting_threads.pop_back();
}

void ServerSession::ClientDisconnected() {
    // We keep a shared pointer to the hle handler to keep it alive throughout
    // the call to ClientDisconnected, as ClientDisconnected invalidates the
    // hle_handler member itself during the course of the function executing.
    std::shared_ptr<SessionRequestHandler> handler = hle_handler;
    if (handler) {
        // Note that after this returns, this server session's hle_handler is
        // invalidated (set to null).
        handler->ClientDisconnected(this);
    }

    // Clean up the list of client threads with pending requests, they are unneeded now that the
    // client endpoint is closed.
    pending_requesting_threads.clear();
    currently_handling = nullptr;
}

void ServerSession::AppendDomainRequestHandler(std::shared_ptr<SessionRequestHandler> handler) {
    domain_request_handlers.push_back(std::move(handler));
}

std::size_t ServerSession::NumDomainRequestHandlers() const {
    return domain_request_handlers.size();
}

ResultCode ServerSession::HandleDomainSyncRequest(Kernel::HLERequestContext& context) {
    if (!context.HasDomainMessageHeader()) {
        return RESULT_SUCCESS;
    }

    // Set domain handlers in HLE context, used for domain objects (IPC interfaces) as inputs
    context.SetDomainRequestHandlers(domain_request_handlers);

    // If there is a DomainMessageHeader, then this is CommandType "Request"
    const auto& domain_message_header = context.GetDomainMessageHeader();
    const u32 object_id{domain_message_header.object_id};
    switch (domain_message_header.command) {
    case IPC::DomainMessageHeader::CommandType::SendMessage:
        if (object_id > domain_request_handlers.size()) {
            LOG_CRITICAL(IPC,
                         "object_id {} is too big! This probably means a recent service call "
                         "to {} needed to return a new interface!",
                         object_id, name);
            UNREACHABLE();
            return RESULT_SUCCESS; // Ignore error if asserts are off
        }
        return domain_request_handlers[object_id - 1]->HandleSyncRequest(context);

    case IPC::DomainMessageHeader::CommandType::CloseVirtualHandle: {
        LOG_DEBUG(IPC, "CloseVirtualHandle, object_id=0x{:08X}", object_id);

        domain_request_handlers[object_id - 1] = nullptr;

        IPC::ResponseBuilder rb{context, 2};
        rb.Push(RESULT_SUCCESS);
        return RESULT_SUCCESS;
    }
    }

    LOG_CRITICAL(IPC, "Unknown domain command={}",
                 static_cast<int>(domain_message_header.command.Value()));
    ASSERT(false);
    return RESULT_SUCCESS;
}

ResultCode ServerSession::HandleSyncRequest(SharedPtr<Thread> thread) {
    // The ServerSession received a sync request, this means that there's new data available
    // from its ClientSession, so wake up any threads that may be waiting on a svcReplyAndReceive or
    // similar.
    Kernel::HLERequestContext context(this, thread);
    u32* cmd_buf = (u32*)Memory::GetPointer(thread->GetTLSAddress());
    context.PopulateFromIncomingCommandBuffer(kernel.CurrentProcess()->GetHandleTable(), cmd_buf);

    ResultCode result = RESULT_SUCCESS;
    // If the session has been converted to a domain, handle the domain request
    if (IsDomain() && context.HasDomainMessageHeader()) {
        result = HandleDomainSyncRequest(context);
        // If there is no domain header, the regular session handler is used
    } else if (hle_handler != nullptr) {
        // If this ServerSession has an associated HLE handler, forward the request to it.
        result = hle_handler->HandleSyncRequest(context);
    }

    if (thread->GetStatus() == ThreadStatus::Running) {
        // Put the thread to sleep until the server replies, it will be awoken in
        // svcReplyAndReceive for LLE servers.
        thread->SetStatus(ThreadStatus::WaitIPC);

        if (hle_handler != nullptr) {
            // For HLE services, we put the request threads to sleep for a short duration to
            // simulate IPC overhead, but only if the HLE handler didn't put the thread to sleep for
            // other reasons like an async callback. The IPC overhead is needed to prevent
            // starvation when a thread only does sync requests to HLE services while a
            // lower-priority thread is waiting to run.

            // This delay was approximated in a homebrew application by measuring the average time
            // it takes for svcSendSyncRequest to return when performing the SetLcdForceBlack IPC
            // request to the GSP:GPU service in a n3DS with firmware 11.6. The measured values have
            // a high variance and vary between models.
            static constexpr u64 IPCDelayNanoseconds = 39000;
            thread->WakeAfterDelay(IPCDelayNanoseconds);
        } else {
            // Add the thread to the list of threads that have issued a sync request with this
            // server.
            pending_requesting_threads.push_back(std::move(thread));
        }
    }

    // If this ServerSession does not have an HLE implementation, just wake up the threads waiting
    // on it.
    WakeupAllWaitingThreads();

    // Handle scenario when ConvertToDomain command was issued, as we must do the conversion at the
    // end of the command such that only commands following this one are handled as domains
    if (convert_to_domain) {
        ASSERT_MSG(IsSession(), "ServerSession is already a domain instance.");
        domain_request_handlers = {hle_handler};
        convert_to_domain = false;
    }

    return result;
}

ServerSession::SessionPair ServerSession::CreateSessionPair(KernelCore& kernel,
                                                            const std::string& name,
                                                            SharedPtr<ClientPort> port) {
    auto server_session = ServerSession::Create(kernel, name + "_Server").Unwrap();
    SharedPtr<ClientSession> client_session(new ClientSession(kernel));
    client_session->name = name + "_Client";

    std::shared_ptr<Session> parent(new Session);
    parent->client = client_session.get();
    parent->server = server_session.get();
    parent->port = std::move(port);

    client_session->parent = parent;
    server_session->parent = parent;

    return std::make_pair(std::move(server_session), std::move(client_session));
}
} // namespace Kernel
