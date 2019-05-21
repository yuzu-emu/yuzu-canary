// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include <boost/container/small_vector.hpp>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/object.h"

union ResultCode;

namespace Service {
class ServiceFrameworkBase;
}

namespace Kernel {

class Domain;
class HandleTable;
class HLERequestContext;
class Process;
class ServerSession;
class Thread;
class ReadableEvent;
class WritableEvent;

enum class ThreadWakeupReason;

/**
 * Interface implemented by HLE Session handlers.
 * This can be provided to a ServerSession in order to hook into several relevant events
 * (such as a new connection or a SyncRequest) so they can be implemented in the emulator.
 */
class SessionRequestHandler : public std::enable_shared_from_this<SessionRequestHandler> {
public:
    SessionRequestHandler();
    virtual ~SessionRequestHandler();

    /**
     * Handles a sync request from the emulated application.
     * @param server_session The ServerSession that was triggered for this sync request,
     * it should be used to differentiate which client (As in ClientSession) we're answering to.
     * TODO(Subv): Use a wrapper structure to hold all the information relevant to
     * this request (ServerSession, Originator thread, Translated command buffer, etc).
     * @returns ResultCode the result code of the translate operation.
     */
    virtual ResultCode HandleSyncRequest(Kernel::HLERequestContext& context) = 0;

    /**
     * Signals that a client has just connected to this HLE handler and keeps the
     * associated ServerSession alive for the duration of the connection.
     * @param server_session Owning pointer to the ServerSession associated with the connection.
     */
    void ClientConnected(SharedPtr<ServerSession> server_session);

    /**
     * Signals that a client has just disconnected from this HLE handler and releases the
     * associated ServerSession.
     * @param server_session ServerSession associated with the connection.
     */
    void ClientDisconnected(const SharedPtr<ServerSession>& server_session);

protected:
    /// List of sessions that are connected to this handler.
    /// A ServerSession whose server endpoint is an HLE implementation is kept alive by this list
    /// for the duration of the connection.
    std::vector<SharedPtr<ServerSession>> connected_sessions;
};

/**
 * Class containing information about an in-flight IPC request being handled by an HLE service
 * implementation. Services should avoid using old global APIs (e.g. Kernel::GetCommandBuffer()) and
 * when possible use the APIs in this class to service the request.
 *
 * HLE handle protocol
 * ===================
 *
 * To avoid needing HLE services to keep a separate handle table, or having to directly modify the
 * requester's table, a tweaked protocol is used to receive and send handles in requests. The kernel
 * will decode the incoming handles into object pointers and insert a id in the buffer where the
 * handle would normally be. The service then calls GetIncomingHandle() with that id to get the
 * pointer to the object. Similarly, instead of inserting a handle into the command buffer, the
 * service calls AddOutgoingHandle() and stores the returned id where the handle would normally go.
 *
 * The end result is similar to just giving services their own real handle tables, but since these
 * ids are local to a specific context, it avoids requiring services to manage handles for objects
 * across multiple calls and ensuring that unneeded handles are cleaned up.
 */
class HLERequestContext {
public:
    explicit HLERequestContext(SharedPtr<ServerSession> session, SharedPtr<Thread> thread);
    ~HLERequestContext();

    /// Returns a pointer to the IPC command buffer for this request.
    u32* CommandBuffer() {
        return cmd_buf.data();
    }

    /**
     * Returns the session through which this request was made. This can be used as a map key to
     * access per-client data on services.
     */
    const SharedPtr<Kernel::ServerSession>& Session() const {
        return server_session;
    }

    using WakeupCallback = std::function<void(SharedPtr<Thread> thread, HLERequestContext& context,
                                              ThreadWakeupReason reason)>;

    /**
     * Puts the specified guest thread to sleep until the returned event is signaled or until the
     * specified timeout expires.
     * @param reason Reason for pausing the thread, to be used for debugging purposes.
     * @param timeout Timeout in nanoseconds after which the thread will be awoken and the callback
     * invoked with a Timeout reason.
     * @param callback Callback to be invoked when the thread is resumed. This callback must write
     * the entire command response once again, regardless of the state of it before this function
     * was called.
     * @param writable_event Event to use to wake up the thread. If unspecified, an event will be
     * created.
     * @returns Event that when signaled will resume the thread and call the callback function.
     */
    SharedPtr<WritableEvent> SleepClientThread(const std::string& reason, u64 timeout,
                                               WakeupCallback&& callback,
                                               SharedPtr<WritableEvent> writable_event = nullptr);

    /// Populates this context with data from the requesting process/thread.
    ResultCode PopulateFromIncomingCommandBuffer(const HandleTable& handle_table,
                                                 u32_le* src_cmdbuf);

    /// Writes data from this context back to the requesting process/thread.
    ResultCode WriteToOutgoingCommandBuffer(Thread& thread);

    u32_le GetCommand() const {
        return command;
    }

    IPC::CommandType GetCommandType() const {
        return command_header->type;
    }

    unsigned GetDataPayloadOffset() const {
        return data_payload_offset;
    }

    const std::vector<IPC::BufferDescriptorX>& BufferDescriptorX() const {
        return buffer_x_desciptors;
    }

    const std::vector<IPC::BufferDescriptorABW>& BufferDescriptorA() const {
        return buffer_a_desciptors;
    }

    const std::vector<IPC::BufferDescriptorABW>& BufferDescriptorB() const {
        return buffer_b_desciptors;
    }

    const std::vector<IPC::BufferDescriptorC>& BufferDescriptorC() const {
        return buffer_c_desciptors;
    }

    const IPC::DomainMessageHeader& GetDomainMessageHeader() const {
        return domain_message_header.value();
    }

    bool HasDomainMessageHeader() const {
        return domain_message_header.has_value();
    }

    /// Helper function to read a buffer using the appropriate buffer descriptor
    std::vector<u8> ReadBuffer(int buffer_index = 0) const;

    /// Helper function to write a buffer using the appropriate buffer descriptor
    std::size_t WriteBuffer(const void* buffer, std::size_t size, int buffer_index = 0) const;

    /* Helper function to write a buffer using the appropriate buffer descriptor
     *
     * @tparam ContiguousContainer an arbitrary container that satisfies the
     *         ContiguousContainer concept in the C++ standard library.
     *
     * @param container    The container to write the data of into a buffer.
     * @param buffer_index The buffer in particular to write to.
     */
    template <typename ContiguousContainer,
              typename = std::enable_if_t<!std::is_pointer_v<ContiguousContainer>>>
    std::size_t WriteBuffer(const ContiguousContainer& container, int buffer_index = 0) const {
        using ContiguousType = typename ContiguousContainer::value_type;

        static_assert(std::is_trivially_copyable_v<ContiguousType>,
                      "Container to WriteBuffer must contain trivially copyable objects");

        return WriteBuffer(std::data(container), std::size(container) * sizeof(ContiguousType),
                           buffer_index);
    }

    /// Helper function to get the size of the input buffer
    std::size_t GetReadBufferSize(int buffer_index = 0) const;

    /// Helper function to get the size of the output buffer
    std::size_t GetWriteBufferSize(int buffer_index = 0) const;

    template <typename T>
    SharedPtr<T> GetCopyObject(std::size_t index) {
        return DynamicObjectCast<T>(copy_objects.at(index));
    }

    template <typename T>
    SharedPtr<T> GetMoveObject(std::size_t index) {
        return DynamicObjectCast<T>(move_objects.at(index));
    }

    void AddMoveObject(SharedPtr<Object> object) {
        move_objects.emplace_back(std::move(object));
    }

    void AddCopyObject(SharedPtr<Object> object) {
        copy_objects.emplace_back(std::move(object));
    }

    void AddDomainObject(std::shared_ptr<SessionRequestHandler> object) {
        domain_objects.emplace_back(std::move(object));
    }

    template <typename T>
    std::shared_ptr<T> GetDomainRequestHandler(std::size_t index) const {
        return std::static_pointer_cast<T>(domain_request_handlers.at(index));
    }

    void SetDomainRequestHandlers(
        const std::vector<std::shared_ptr<SessionRequestHandler>>& handlers) {
        domain_request_handlers = handlers;
    }

    /// Clears the list of objects so that no lingering objects are written accidentally to the
    /// response buffer.
    void ClearIncomingObjects() {
        move_objects.clear();
        copy_objects.clear();
        domain_objects.clear();
    }

    std::size_t NumMoveObjects() const {
        return move_objects.size();
    }

    std::size_t NumCopyObjects() const {
        return copy_objects.size();
    }

    std::size_t NumDomainObjects() const {
        return domain_objects.size();
    }

    std::string Description() const;

private:
    void ParseCommandBuffer(const HandleTable& handle_table, u32_le* src_cmdbuf, bool incoming);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> cmd_buf;
    SharedPtr<Kernel::ServerSession> server_session;
    SharedPtr<Thread> thread;
    // TODO(yuriks): Check common usage of this and optimize size accordingly
    boost::container::small_vector<SharedPtr<Object>, 8> move_objects;
    boost::container::small_vector<SharedPtr<Object>, 8> copy_objects;
    boost::container::small_vector<std::shared_ptr<SessionRequestHandler>, 8> domain_objects;

    std::optional<IPC::CommandHeader> command_header;
    std::optional<IPC::HandleDescriptorHeader> handle_descriptor_header;
    std::optional<IPC::DataPayloadHeader> data_payload_header;
    std::optional<IPC::DomainMessageHeader> domain_message_header;
    std::vector<IPC::BufferDescriptorX> buffer_x_desciptors;
    std::vector<IPC::BufferDescriptorABW> buffer_a_desciptors;
    std::vector<IPC::BufferDescriptorABW> buffer_b_desciptors;
    std::vector<IPC::BufferDescriptorABW> buffer_w_desciptors;
    std::vector<IPC::BufferDescriptorC> buffer_c_desciptors;

    unsigned data_payload_offset{};
    unsigned buffer_c_offset{};
    u32_le command{};

    std::vector<std::shared_ptr<SessionRequestHandler>> domain_request_handlers;
};

} // namespace Kernel
