// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <sstream>
#include <utility>

#include <boost/range/algorithm_ext/erase.hpp>

#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/writable_event.h"
#include "core/memory.h"

namespace Kernel {

SessionRequestHandler::SessionRequestHandler() = default;

SessionRequestHandler::~SessionRequestHandler() = default;

void SessionRequestHandler::ClientConnected(SharedPtr<ServerSession> server_session) {
    server_session->SetHleHandler(shared_from_this());
    connected_sessions.push_back(std::move(server_session));
}

void SessionRequestHandler::ClientDisconnected(const SharedPtr<ServerSession>& server_session) {
    server_session->SetHleHandler(nullptr);
    boost::range::remove_erase(connected_sessions, server_session);
}

SharedPtr<WritableEvent> HLERequestContext::SleepClientThread(
    const std::string& reason, u64 timeout, WakeupCallback&& callback,
    SharedPtr<WritableEvent> writable_event) {
    // Put the client thread to sleep until the wait event is signaled or the timeout expires.
    thread->SetWakeupCallback([context = *this, callback](
                                  ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                  SharedPtr<WaitObject> object, std::size_t index) mutable -> bool {
        ASSERT(thread->GetStatus() == ThreadStatus::WaitHLEEvent);
        callback(thread, context, reason);
        context.WriteToOutgoingCommandBuffer(*thread);
        return true;
    });

    auto& kernel = Core::System::GetInstance().Kernel();
    if (!writable_event) {
        // Create event if not provided
        const auto pair = WritableEvent::CreateEventPair(kernel, ResetType::Automatic,
                                                         "HLE Pause Event: " + reason);
        writable_event = pair.writable;
    }

    const auto readable_event{writable_event->GetReadableEvent()};
    writable_event->Clear();
    thread->SetStatus(ThreadStatus::WaitHLEEvent);
    thread->SetWaitObjects({readable_event});
    readable_event->AddWaitingThread(thread);

    if (timeout > 0) {
        thread->WakeAfterDelay(timeout);
    }

    return writable_event;
}

HLERequestContext::HLERequestContext(SharedPtr<Kernel::ServerSession> server_session,
                                     SharedPtr<Thread> thread)
    : server_session(std::move(server_session)), thread(std::move(thread)) {
    cmd_buf[0] = 0;
}

HLERequestContext::~HLERequestContext() = default;

void HLERequestContext::ParseCommandBuffer(const HandleTable& handle_table, u32_le* src_cmdbuf,
                                           bool incoming) {
    IPC::RequestParser rp(src_cmdbuf);
    command_header = rp.PopRaw<IPC::CommandHeader>();

    if (command_header->type == IPC::CommandType::Close) {
        // Close does not populate the rest of the IPC header
        return;
    }

    // If handle descriptor is present, add size of it
    if (command_header->enable_handle_descriptor) {
        handle_descriptor_header = rp.PopRaw<IPC::HandleDescriptorHeader>();
        if (handle_descriptor_header->send_current_pid) {
            rp.Skip(2, false);
        }
        if (incoming) {
            // Populate the object lists with the data in the IPC request.
            for (u32 handle = 0; handle < handle_descriptor_header->num_handles_to_copy; ++handle) {
                copy_objects.push_back(handle_table.GetGeneric(rp.Pop<Handle>()));
            }
            for (u32 handle = 0; handle < handle_descriptor_header->num_handles_to_move; ++handle) {
                move_objects.push_back(handle_table.GetGeneric(rp.Pop<Handle>()));
            }
        } else {
            // For responses we just ignore the handles, they're empty and will be populated when
            // translating the response.
            rp.Skip(handle_descriptor_header->num_handles_to_copy, false);
            rp.Skip(handle_descriptor_header->num_handles_to_move, false);
        }
    }

    for (unsigned i = 0; i < command_header->num_buf_x_descriptors; ++i) {
        buffer_x_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorX>());
    }
    for (unsigned i = 0; i < command_header->num_buf_a_descriptors; ++i) {
        buffer_a_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorABW>());
    }
    for (unsigned i = 0; i < command_header->num_buf_b_descriptors; ++i) {
        buffer_b_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorABW>());
    }
    for (unsigned i = 0; i < command_header->num_buf_w_descriptors; ++i) {
        buffer_w_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorABW>());
    }

    buffer_c_offset = rp.GetCurrentOffset() + command_header->data_size;

    // Padding to align to 16 bytes
    rp.AlignWithPadding();

    if (Session()->IsDomain() && ((command_header->type == IPC::CommandType::Request ||
                                   command_header->type == IPC::CommandType::RequestWithContext) ||
                                  !incoming)) {
        // If this is an incoming message, only CommandType "Request" has a domain header
        // All outgoing domain messages have the domain header, if only incoming has it
        if (incoming || domain_message_header) {
            domain_message_header = rp.PopRaw<IPC::DomainMessageHeader>();
        } else {
            if (Session()->IsDomain()) {
                LOG_WARNING(IPC, "Domain request has no DomainMessageHeader!");
            }
        }
    }

    data_payload_header = rp.PopRaw<IPC::DataPayloadHeader>();

    data_payload_offset = rp.GetCurrentOffset();

    if (domain_message_header && domain_message_header->command ==
                                     IPC::DomainMessageHeader::CommandType::CloseVirtualHandle) {
        // CloseVirtualHandle command does not have SFC* or any data
        return;
    }

    if (incoming) {
        ASSERT(data_payload_header->magic == Common::MakeMagic('S', 'F', 'C', 'I'));
    } else {
        ASSERT(data_payload_header->magic == Common::MakeMagic('S', 'F', 'C', 'O'));
    }

    rp.SetCurrentOffset(buffer_c_offset);

    // For Inline buffers, the response data is written directly to buffer_c_offset
    // and in this case we don't have any BufferDescriptorC on the request.
    if (command_header->buf_c_descriptor_flags >
        IPC::CommandHeader::BufferDescriptorCFlag::InlineDescriptor) {
        if (command_header->buf_c_descriptor_flags ==
            IPC::CommandHeader::BufferDescriptorCFlag::OneDescriptor) {
            buffer_c_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorC>());
        } else {
            unsigned num_buf_c_descriptors =
                static_cast<unsigned>(command_header->buf_c_descriptor_flags.Value()) - 2;

            // This is used to detect possible underflows, in case something is broken
            // with the two ifs above and the flags value is == 0 || == 1.
            ASSERT(num_buf_c_descriptors < 14);

            for (unsigned i = 0; i < num_buf_c_descriptors; ++i) {
                buffer_c_desciptors.push_back(rp.PopRaw<IPC::BufferDescriptorC>());
            }
        }
    }

    rp.SetCurrentOffset(data_payload_offset);

    command = rp.Pop<u32_le>();
    rp.Skip(1, false); // The command is actually an u64, but we don't use the high part.
}

ResultCode HLERequestContext::PopulateFromIncomingCommandBuffer(const HandleTable& handle_table,
                                                                u32_le* src_cmdbuf) {
    ParseCommandBuffer(handle_table, src_cmdbuf, true);
    if (command_header->type == IPC::CommandType::Close) {
        // Close does not populate the rest of the IPC header
        return RESULT_SUCCESS;
    }

    // The data_size already includes the payload header, the padding and the domain header.
    std::size_t size = data_payload_offset + command_header->data_size -
                       sizeof(IPC::DataPayloadHeader) / sizeof(u32) - 4;
    if (domain_message_header)
        size -= sizeof(IPC::DomainMessageHeader) / sizeof(u32);
    std::copy_n(src_cmdbuf, size, cmd_buf.begin());
    return RESULT_SUCCESS;
}

ResultCode HLERequestContext::WriteToOutgoingCommandBuffer(Thread& thread) {
    auto& owner_process = *thread.GetOwnerProcess();
    auto& handle_table = owner_process.GetHandleTable();

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> dst_cmdbuf;
    Memory::ReadBlock(owner_process, thread.GetTLSAddress(), dst_cmdbuf.data(),
                      dst_cmdbuf.size() * sizeof(u32));

    // The header was already built in the internal command buffer. Attempt to parse it to verify
    // the integrity and then copy it over to the target command buffer.
    ParseCommandBuffer(handle_table, cmd_buf.data(), false);

    // The data_size already includes the payload header, the padding and the domain header.
    std::size_t size = data_payload_offset + command_header->data_size -
                       sizeof(IPC::DataPayloadHeader) / sizeof(u32) - 4;
    if (domain_message_header)
        size -= sizeof(IPC::DomainMessageHeader) / sizeof(u32);

    std::copy_n(cmd_buf.begin(), size, dst_cmdbuf.data());

    if (command_header->enable_handle_descriptor) {
        ASSERT_MSG(!move_objects.empty() || !copy_objects.empty(),
                   "Handle descriptor bit set but no handles to translate");
        // We write the translated handles at a specific offset in the command buffer, this space
        // was already reserved when writing the header.
        std::size_t current_offset =
            (sizeof(IPC::CommandHeader) + sizeof(IPC::HandleDescriptorHeader)) / sizeof(u32);
        ASSERT_MSG(!handle_descriptor_header->send_current_pid, "Sending PID is not implemented");

        ASSERT(copy_objects.size() == handle_descriptor_header->num_handles_to_copy);
        ASSERT(move_objects.size() == handle_descriptor_header->num_handles_to_move);

        // We don't make a distinction between copy and move handles when translating since HLE
        // services don't deal with handles directly. However, the guest applications might check
        // for specific values in each of these descriptors.
        for (auto& object : copy_objects) {
            ASSERT(object != nullptr);
            dst_cmdbuf[current_offset++] = handle_table.Create(object).Unwrap();
        }

        for (auto& object : move_objects) {
            ASSERT(object != nullptr);
            dst_cmdbuf[current_offset++] = handle_table.Create(object).Unwrap();
        }
    }

    // TODO(Subv): Translate the X/A/B/W buffers.

    if (Session()->IsDomain() && domain_message_header) {
        ASSERT(domain_message_header->num_objects == domain_objects.size());
        // Write the domain objects to the command buffer, these go after the raw untranslated data.
        // TODO(Subv): This completely ignores C buffers.
        std::size_t domain_offset = size - domain_message_header->num_objects;

        for (const auto& object : domain_objects) {
            server_session->AppendDomainRequestHandler(object);
            dst_cmdbuf[domain_offset++] =
                static_cast<u32_le>(server_session->NumDomainRequestHandlers());
        }
    }

    // Copy the translated command buffer back into the thread's command buffer area.
    Memory::WriteBlock(owner_process, thread.GetTLSAddress(), dst_cmdbuf.data(),
                       dst_cmdbuf.size() * sizeof(u32));

    return RESULT_SUCCESS;
}

std::vector<u8> HLERequestContext::ReadBuffer(int buffer_index) const {
    std::vector<u8> buffer;
    const bool is_buffer_a{BufferDescriptorA().size() && BufferDescriptorA()[buffer_index].Size()};

    if (is_buffer_a) {
        buffer.resize(BufferDescriptorA()[buffer_index].Size());
        Memory::ReadBlock(BufferDescriptorA()[buffer_index].Address(), buffer.data(),
                          buffer.size());
    } else {
        buffer.resize(BufferDescriptorX()[buffer_index].Size());
        Memory::ReadBlock(BufferDescriptorX()[buffer_index].Address(), buffer.data(),
                          buffer.size());
    }

    return buffer;
}

std::size_t HLERequestContext::WriteBuffer(const void* buffer, std::size_t size,
                                           int buffer_index) const {
    if (size == 0) {
        LOG_WARNING(Core, "skip empty buffer write");
        return 0;
    }

    const bool is_buffer_b{BufferDescriptorB().size() && BufferDescriptorB()[buffer_index].Size()};
    const std::size_t buffer_size{GetWriteBufferSize(buffer_index)};
    if (size > buffer_size) {
        LOG_CRITICAL(Core, "size ({:016X}) is greater than buffer_size ({:016X})", size,
                     buffer_size);
        size = buffer_size; // TODO(bunnei): This needs to be HW tested
    }

    if (is_buffer_b) {
        Memory::WriteBlock(BufferDescriptorB()[buffer_index].Address(), buffer, size);
    } else {
        Memory::WriteBlock(BufferDescriptorC()[buffer_index].Address(), buffer, size);
    }

    return size;
}

std::size_t HLERequestContext::GetReadBufferSize(int buffer_index) const {
    const bool is_buffer_a{BufferDescriptorA().size() && BufferDescriptorA()[buffer_index].Size()};
    return is_buffer_a ? BufferDescriptorA()[buffer_index].Size()
                       : BufferDescriptorX()[buffer_index].Size();
}

std::size_t HLERequestContext::GetWriteBufferSize(int buffer_index) const {
    const bool is_buffer_b{BufferDescriptorB().size() && BufferDescriptorB()[buffer_index].Size()};
    return is_buffer_b ? BufferDescriptorB()[buffer_index].Size()
                       : BufferDescriptorC()[buffer_index].Size();
}

std::string HLERequestContext::Description() const {
    if (!command_header) {
        return "No command header available";
    }
    std::ostringstream s;
    s << "IPC::CommandHeader: Type:" << static_cast<u32>(command_header->type.Value());
    s << ", X(Pointer):" << command_header->num_buf_x_descriptors;
    if (command_header->num_buf_x_descriptors) {
        s << '[';
        for (u64 i = 0; i < command_header->num_buf_x_descriptors; ++i) {
            s << "0x" << std::hex << BufferDescriptorX()[i].Size();
            if (i < command_header->num_buf_x_descriptors - 1)
                s << ", ";
        }
        s << ']';
    }
    s << ", A(Send):" << command_header->num_buf_a_descriptors;
    if (command_header->num_buf_a_descriptors) {
        s << '[';
        for (u64 i = 0; i < command_header->num_buf_a_descriptors; ++i) {
            s << "0x" << std::hex << BufferDescriptorA()[i].Size();
            if (i < command_header->num_buf_a_descriptors - 1)
                s << ", ";
        }
        s << ']';
    }
    s << ", B(Receive):" << command_header->num_buf_b_descriptors;
    if (command_header->num_buf_b_descriptors) {
        s << '[';
        for (u64 i = 0; i < command_header->num_buf_b_descriptors; ++i) {
            s << "0x" << std::hex << BufferDescriptorB()[i].Size();
            if (i < command_header->num_buf_b_descriptors - 1)
                s << ", ";
        }
        s << ']';
    }
    s << ", C(ReceiveList):" << BufferDescriptorC().size();
    if (!BufferDescriptorC().empty()) {
        s << '[';
        for (u64 i = 0; i < BufferDescriptorC().size(); ++i) {
            s << "0x" << std::hex << BufferDescriptorC()[i].Size();
            if (i < BufferDescriptorC().size() - 1)
                s << ", ";
        }
        s << ']';
    }
    s << ", data_size:" << command_header->data_size.Value();

    return s.str();
}

} // namespace Kernel
