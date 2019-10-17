// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <iterator>
#include <mutex>
#include <vector>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/string_util.h"
#include "core/arm/exclusive_monitor.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/hle/kernel/address_arbiter.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/svc_wrap.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/transfer_memory.h"
#include "core/hle/kernel/writable_event.h"
#include "core/hle/lock.h"
#include "core/hle/result.h"
#include "core/hle/service/service.h"
#include "core/memory.h"
#include "core/reporter.h"

namespace Kernel {
namespace {

// Checks if address + size is greater than the given address
// This can return false if the size causes an overflow of a 64-bit type
// or if the given size is zero.
constexpr bool IsValidAddressRange(VAddr address, u64 size) {
    return address + size > address;
}

// 8 GiB
constexpr u64 MAIN_MEMORY_SIZE = 0x200000000;

// Helper function that performs the common sanity checks for svcMapMemory
// and svcUnmapMemory. This is doable, as both functions perform their sanitizing
// in the same order.
ResultCode MapUnmapMemorySanityChecks(const VMManager& vm_manager, VAddr dst_addr, VAddr src_addr,
                                      u64 size) {
    if (!Common::Is4KBAligned(dst_addr)) {
        LOG_ERROR(Kernel_SVC, "Destination address is not aligned to 4KB, 0x{:016X}", dst_addr);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(src_addr)) {
        LOG_ERROR(Kernel_SVC, "Source address is not aligned to 4KB, 0x{:016X}", src_addr);
        return ERR_INVALID_SIZE;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        return ERR_INVALID_SIZE;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, 0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(dst_addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination is not a valid address range, addr=0x{:016X}, size=0x{:016X}",
                  dst_addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!IsValidAddressRange(src_addr, size)) {
        LOG_ERROR(Kernel_SVC, "Source is not a valid address range, addr=0x{:016X}, size=0x{:016X}",
                  src_addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!vm_manager.IsWithinAddressSpace(src_addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source is not within the address space, addr=0x{:016X}, size=0x{:016X}",
                  src_addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!vm_manager.IsWithinStackRegion(dst_addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination is not within the stack region, addr=0x{:016X}, size=0x{:016X}",
                  dst_addr, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    const VAddr dst_end_address = dst_addr + size;
    if (dst_end_address > vm_manager.GetHeapRegionBaseAddress() &&
        vm_manager.GetHeapRegionEndAddress() > dst_addr) {
        LOG_ERROR(Kernel_SVC,
                  "Destination does not fit within the heap region, addr=0x{:016X}, "
                  "size=0x{:016X}, end_addr=0x{:016X}",
                  dst_addr, size, dst_end_address);
        return ERR_INVALID_MEMORY_RANGE;
    }

    if (dst_end_address > vm_manager.GetMapRegionBaseAddress() &&
        vm_manager.GetMapRegionEndAddress() > dst_addr) {
        LOG_ERROR(Kernel_SVC,
                  "Destination does not fit within the map region, addr=0x{:016X}, "
                  "size=0x{:016X}, end_addr=0x{:016X}",
                  dst_addr, size, dst_end_address);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return RESULT_SUCCESS;
}

enum class ResourceLimitValueType {
    CurrentValue,
    LimitValue,
};

ResultVal<s64> RetrieveResourceLimitValue(Core::System& system, Handle resource_limit,
                                          u32 resource_type, ResourceLimitValueType value_type) {
    const auto type = static_cast<ResourceType>(resource_type);
    if (!IsValidResourceType(type)) {
        LOG_ERROR(Kernel_SVC, "Invalid resource limit type: '{}'", resource_type);
        return ERR_INVALID_ENUM_VALUE;
    }

    const auto* const current_process = system.Kernel().CurrentProcess();
    ASSERT(current_process != nullptr);

    const auto resource_limit_object =
        current_process->GetHandleTable().Get<ResourceLimit>(resource_limit);
    if (!resource_limit_object) {
        LOG_ERROR(Kernel_SVC, "Handle to non-existent resource limit instance used. Handle={:08X}",
                  resource_limit);
        return ERR_INVALID_HANDLE;
    }

    if (value_type == ResourceLimitValueType::CurrentValue) {
        return MakeResult(resource_limit_object->GetCurrentResourceValue(type));
    }

    return MakeResult(resource_limit_object->GetMaxResourceValue(type));
}
} // Anonymous namespace

/// Set the process heap to a given Size. It can both extend and shrink the heap.
static ResultCode SetHeapSize(Core::System& system, VAddr* heap_addr, u64 heap_size) {
    LOG_TRACE(Kernel_SVC, "called, heap_size=0x{:X}", heap_size);

    // Size must be a multiple of 0x200000 (2MB) and be equal to or less than 8GB.
    if ((heap_size % 0x200000) != 0) {
        LOG_ERROR(Kernel_SVC, "The heap size is not a multiple of 2MB, heap_size=0x{:016X}",
                  heap_size);
        return ERR_INVALID_SIZE;
    }

    if (heap_size >= 0x200000000) {
        LOG_ERROR(Kernel_SVC, "The heap size is not less than 8GB, heap_size=0x{:016X}", heap_size);
        return ERR_INVALID_SIZE;
    }

    auto& vm_manager = system.Kernel().CurrentProcess()->VMManager();
    const auto alloc_result = vm_manager.SetHeapSize(heap_size);
    if (alloc_result.Failed()) {
        return alloc_result.Code();
    }

    *heap_addr = *alloc_result;
    return RESULT_SUCCESS;
}

static ResultCode SetMemoryPermission(Core::System& system, VAddr addr, u64 size, u32 prot) {
    LOG_TRACE(Kernel_SVC, "called, addr=0x{:X}, size=0x{:X}, prot=0x{:X}", addr, size, prot);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address is not aligned to 4KB, addr=0x{:016X}", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        return ERR_INVALID_SIZE;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, size=0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Region is not a valid address range, addr=0x{:016X}, size=0x{:016X}",
                  addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto permission = static_cast<MemoryPermission>(prot);
    if (permission != MemoryPermission::None && permission != MemoryPermission::Read &&
        permission != MemoryPermission::ReadWrite) {
        LOG_ERROR(Kernel_SVC, "Invalid memory permission specified, Got memory permission=0x{:08X}",
                  static_cast<u32>(permission));
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    auto& vm_manager = current_process->VMManager();

    if (!vm_manager.IsWithinAddressSpace(addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source is not within the address space, addr=0x{:016X}, size=0x{:016X}", addr,
                  size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const VMManager::VMAHandle iter = vm_manager.FindVMA(addr);
    if (!vm_manager.IsValidHandle(iter)) {
        LOG_ERROR(Kernel_SVC, "Unable to find VMA for address=0x{:016X}", addr);
        return ERR_INVALID_ADDRESS_STATE;
    }

    LOG_WARNING(Kernel_SVC, "Uniformity check on protected memory is not implemented.");
    // TODO: Performs a uniformity check to make sure only protected memory is changed (it doesn't
    // make sense to allow changing permissions on kernel memory itself, etc).

    const auto converted_permissions = SharedMemory::ConvertPermissions(permission);

    return vm_manager.ReprotectRange(addr, size, converted_permissions);
}

static ResultCode SetMemoryAttribute(Core::System& system, VAddr address, u64 size, u32 mask,
                                     u32 attribute) {
    LOG_DEBUG(Kernel_SVC,
              "called, address=0x{:016X}, size=0x{:X}, mask=0x{:08X}, attribute=0x{:08X}", address,
              size, mask, attribute);

    if (!Common::Is4KBAligned(address)) {
        LOG_ERROR(Kernel_SVC, "Address not page aligned (0x{:016X})", address);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0 || !Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Invalid size (0x{:X}). Size must be non-zero and page aligned.",
                  size);
        return ERR_INVALID_ADDRESS;
    }

    if (!IsValidAddressRange(address, size)) {
        LOG_ERROR(Kernel_SVC, "Address range overflowed (Address: 0x{:016X}, Size: 0x{:016X})",
                  address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto mem_attribute = static_cast<MemoryAttribute>(attribute);
    const auto mem_mask = static_cast<MemoryAttribute>(mask);
    const auto attribute_with_mask = mem_attribute | mem_mask;

    if (attribute_with_mask != mem_mask) {
        LOG_ERROR(Kernel_SVC,
                  "Memory attribute doesn't match the given mask (Attribute: 0x{:X}, Mask: {:X}",
                  attribute, mask);
        return ERR_INVALID_COMBINATION;
    }

    if ((attribute_with_mask | MemoryAttribute::Uncached) != MemoryAttribute::Uncached) {
        LOG_ERROR(Kernel_SVC, "Specified attribute isn't equal to MemoryAttributeUncached (8).");
        return ERR_INVALID_COMBINATION;
    }

    auto& vm_manager = system.Kernel().CurrentProcess()->VMManager();
    if (!vm_manager.IsWithinAddressSpace(address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Given address (0x{:016X}) is outside the bounds of the address space.", address);
        return ERR_INVALID_ADDRESS_STATE;
    }

    return vm_manager.SetMemoryAttribute(address, size, mem_mask, mem_attribute);
}

/// Maps a memory range into a different range.
static ResultCode MapMemory(Core::System& system, VAddr dst_addr, VAddr src_addr, u64 size) {
    LOG_TRACE(Kernel_SVC, "called, dst_addr=0x{:X}, src_addr=0x{:X}, size=0x{:X}", dst_addr,
              src_addr, size);

    auto& vm_manager = system.Kernel().CurrentProcess()->VMManager();
    const auto result = MapUnmapMemorySanityChecks(vm_manager, dst_addr, src_addr, size);

    if (result.IsError()) {
        return result;
    }

    return vm_manager.MirrorMemory(dst_addr, src_addr, size, MemoryState::Stack);
}

/// Unmaps a region that was previously mapped with svcMapMemory
static ResultCode UnmapMemory(Core::System& system, VAddr dst_addr, VAddr src_addr, u64 size) {
    LOG_TRACE(Kernel_SVC, "called, dst_addr=0x{:X}, src_addr=0x{:X}, size=0x{:X}", dst_addr,
              src_addr, size);

    auto& vm_manager = system.Kernel().CurrentProcess()->VMManager();
    const auto result = MapUnmapMemorySanityChecks(vm_manager, dst_addr, src_addr, size);

    if (result.IsError()) {
        return result;
    }

    const auto unmap_res = vm_manager.UnmapRange(dst_addr, size);

    // Reprotect the source mapping on success
    if (unmap_res.IsSuccess()) {
        ASSERT(vm_manager.ReprotectRange(src_addr, size, VMAPermission::ReadWrite).IsSuccess());
    }

    return unmap_res;
}

/// Connect to an OS service given the port name, returns the handle to the port to out
static ResultCode ConnectToNamedPort(Core::System& system, Handle* out_handle,
                                     VAddr port_name_address) {
    if (!Memory::IsValidVirtualAddress(port_name_address)) {
        LOG_ERROR(Kernel_SVC,
                  "Port Name Address is not a valid virtual address, port_name_address=0x{:016X}",
                  port_name_address);
        return ERR_NOT_FOUND;
    }

    static constexpr std::size_t PortNameMaxLength = 11;
    // Read 1 char beyond the max allowed port name to detect names that are too long.
    std::string port_name = Memory::ReadCString(port_name_address, PortNameMaxLength + 1);
    if (port_name.size() > PortNameMaxLength) {
        LOG_ERROR(Kernel_SVC, "Port name is too long, expected {} but got {}", PortNameMaxLength,
                  port_name.size());
        return ERR_OUT_OF_RANGE;
    }

    LOG_TRACE(Kernel_SVC, "called port_name={}", port_name);

    auto& kernel = system.Kernel();
    const auto it = kernel.FindNamedPort(port_name);
    if (!kernel.IsValidNamedPort(it)) {
        LOG_WARNING(Kernel_SVC, "tried to connect to unknown port: {}", port_name);
        return ERR_NOT_FOUND;
    }

    auto client_port = it->second;

    SharedPtr<ClientSession> client_session;
    CASCADE_RESULT(client_session, client_port->Connect());

    // Return the client session
    auto& handle_table = kernel.CurrentProcess()->GetHandleTable();
    CASCADE_RESULT(*out_handle, handle_table.Create(client_session));
    return RESULT_SUCCESS;
}

/// Makes a blocking IPC call to an OS service.
static ResultCode SendSyncRequest(Core::System& system, Handle handle) {
    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    SharedPtr<ClientSession> session = handle_table.Get<ClientSession>(handle);
    if (!session) {
        LOG_ERROR(Kernel_SVC, "called with invalid handle=0x{:08X}", handle);
        return ERR_INVALID_HANDLE;
    }

    LOG_TRACE(Kernel_SVC, "called handle=0x{:08X}({})", handle, session->GetName());

    system.PrepareReschedule();

    // TODO(Subv): svcSendSyncRequest should put the caller thread to sleep while the server
    // responds and cause a reschedule.
    return session->SendSyncRequest(system.CurrentScheduler().GetCurrentThread());
}

/// Get the ID for the specified thread.
static ResultCode GetThreadId(Core::System& system, u64* thread_id, Handle thread_handle) {
    LOG_TRACE(Kernel_SVC, "called thread=0x{:08X}", thread_handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}", thread_handle);
        return ERR_INVALID_HANDLE;
    }

    *thread_id = thread->GetThreadID();
    return RESULT_SUCCESS;
}

/// Gets the ID of the specified process or a specified thread's owning process.
static ResultCode GetProcessId(Core::System& system, u64* process_id, Handle handle) {
    LOG_DEBUG(Kernel_SVC, "called handle=0x{:08X}", handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const SharedPtr<Process> process = handle_table.Get<Process>(handle);
    if (process) {
        *process_id = process->GetProcessID();
        return RESULT_SUCCESS;
    }

    const SharedPtr<Thread> thread = handle_table.Get<Thread>(handle);
    if (thread) {
        const Process* const owner_process = thread->GetOwnerProcess();
        if (!owner_process) {
            LOG_ERROR(Kernel_SVC, "Non-existent owning process encountered.");
            return ERR_INVALID_HANDLE;
        }

        *process_id = owner_process->GetProcessID();
        return RESULT_SUCCESS;
    }

    // NOTE: This should also handle debug objects before returning.

    LOG_ERROR(Kernel_SVC, "Handle does not exist, handle=0x{:08X}", handle);
    return ERR_INVALID_HANDLE;
}

/// Default thread wakeup callback for WaitSynchronization
static bool DefaultThreadWakeupCallback(ThreadWakeupReason reason, SharedPtr<Thread> thread,
                                        SharedPtr<WaitObject> object, std::size_t index) {
    ASSERT(thread->GetStatus() == ThreadStatus::WaitSynch);

    if (reason == ThreadWakeupReason::Timeout) {
        thread->SetWaitSynchronizationResult(RESULT_TIMEOUT);
        return true;
    }

    ASSERT(reason == ThreadWakeupReason::Signal);
    thread->SetWaitSynchronizationResult(RESULT_SUCCESS);
    thread->SetWaitSynchronizationOutput(static_cast<u32>(index));
    return true;
};

/// Wait for the given handles to synchronize, timeout after the specified nanoseconds
static ResultCode WaitSynchronization(Core::System& system, Handle* index, VAddr handles_address,
                                      u64 handle_count, s64 nano_seconds) {
    LOG_TRACE(Kernel_SVC, "called handles_address=0x{:X}, handle_count={}, nano_seconds={}",
              handles_address, handle_count, nano_seconds);

    if (!Memory::IsValidVirtualAddress(handles_address)) {
        LOG_ERROR(Kernel_SVC,
                  "Handle address is not a valid virtual address, handle_address=0x{:016X}",
                  handles_address);
        return ERR_INVALID_POINTER;
    }

    static constexpr u64 MaxHandles = 0x40;

    if (handle_count > MaxHandles) {
        LOG_ERROR(Kernel_SVC, "Handle count specified is too large, expected {} but got {}",
                  MaxHandles, handle_count);
        return ERR_OUT_OF_RANGE;
    }

    auto* const thread = system.CurrentScheduler().GetCurrentThread();

    using ObjectPtr = Thread::ThreadWaitObjects::value_type;
    Thread::ThreadWaitObjects objects(handle_count);
    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();

    for (u64 i = 0; i < handle_count; ++i) {
        const Handle handle = Memory::Read32(handles_address + i * sizeof(Handle));
        const auto object = handle_table.Get<WaitObject>(handle);

        if (object == nullptr) {
            LOG_ERROR(Kernel_SVC, "Object is a nullptr");
            return ERR_INVALID_HANDLE;
        }

        objects[i] = object;
    }

    // Find the first object that is acquirable in the provided list of objects
    auto itr = std::find_if(objects.begin(), objects.end(), [thread](const ObjectPtr& object) {
        return !object->ShouldWait(thread);
    });

    if (itr != objects.end()) {
        // We found a ready object, acquire it and set the result value
        WaitObject* object = itr->get();
        object->Acquire(thread);
        *index = static_cast<s32>(std::distance(objects.begin(), itr));
        return RESULT_SUCCESS;
    }

    // No objects were ready to be acquired, prepare to suspend the thread.

    // If a timeout value of 0 was provided, just return the Timeout error code instead of
    // suspending the thread.
    if (nano_seconds == 0) {
        return RESULT_TIMEOUT;
    }

    for (auto& object : objects) {
        object->AddWaitingThread(thread);
    }

    thread->SetWaitObjects(std::move(objects));
    thread->SetStatus(ThreadStatus::WaitSynch);

    // Create an event to wake the thread up after the specified nanosecond delay has passed
    thread->WakeAfterDelay(nano_seconds);
    thread->SetWakeupCallback(DefaultThreadWakeupCallback);

    system.PrepareReschedule(thread->GetProcessorID());

    return RESULT_TIMEOUT;
}

/// Resumes a thread waiting on WaitSynchronization
static ResultCode CancelSynchronization(Core::System& system, Handle thread_handle) {
    LOG_TRACE(Kernel_SVC, "called thread=0x{:X}", thread_handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, thread_handle=0x{:08X}",
                  thread_handle);
        return ERR_INVALID_HANDLE;
    }

    thread->CancelWait();
    system.PrepareReschedule(thread->GetProcessorID());
    return RESULT_SUCCESS;
}

/// Attempts to locks a mutex, creating it if it does not already exist
static ResultCode ArbitrateLock(Core::System& system, Handle holding_thread_handle,
                                VAddr mutex_addr, Handle requesting_thread_handle) {
    LOG_TRACE(Kernel_SVC,
              "called holding_thread_handle=0x{:08X}, mutex_addr=0x{:X}, "
              "requesting_current_thread_handle=0x{:08X}",
              holding_thread_handle, mutex_addr, requesting_thread_handle);

    if (Memory::IsKernelVirtualAddress(mutex_addr)) {
        LOG_ERROR(Kernel_SVC, "Mutex Address is a kernel virtual address, mutex_addr={:016X}",
                  mutex_addr);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!Common::IsWordAligned(mutex_addr)) {
        LOG_ERROR(Kernel_SVC, "Mutex Address is not word aligned, mutex_addr={:016X}", mutex_addr);
        return ERR_INVALID_ADDRESS;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    return current_process->GetMutex().TryAcquire(mutex_addr, holding_thread_handle,
                                                  requesting_thread_handle);
}

/// Unlock a mutex
static ResultCode ArbitrateUnlock(Core::System& system, VAddr mutex_addr) {
    LOG_TRACE(Kernel_SVC, "called mutex_addr=0x{:X}", mutex_addr);

    if (Memory::IsKernelVirtualAddress(mutex_addr)) {
        LOG_ERROR(Kernel_SVC, "Mutex Address is a kernel virtual address, mutex_addr={:016X}",
                  mutex_addr);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!Common::IsWordAligned(mutex_addr)) {
        LOG_ERROR(Kernel_SVC, "Mutex Address is not word aligned, mutex_addr={:016X}", mutex_addr);
        return ERR_INVALID_ADDRESS;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    return current_process->GetMutex().Release(mutex_addr,
                                               system.CurrentScheduler().GetCurrentThread());
}

enum class BreakType : u32 {
    Panic = 0,
    AssertionFailed = 1,
    PreNROLoad = 3,
    PostNROLoad = 4,
    PreNROUnload = 5,
    PostNROUnload = 6,
    CppException = 7,
};

struct BreakReason {
    union {
        u32 raw;
        BitField<0, 30, BreakType> break_type;
        BitField<31, 1, u32> signal_debugger;
    };
};

/// Break program execution
static void Break(Core::System& system, u32 reason, u64 info1, u64 info2) {
    BreakReason break_reason{reason};
    bool has_dumped_buffer{};
    std::vector<u8> debug_buffer;

    const auto handle_debug_buffer = [&](VAddr addr, u64 sz) {
        if (sz == 0 || addr == 0 || has_dumped_buffer) {
            return;
        }

        // This typically is an error code so we're going to assume this is the case
        if (sz == sizeof(u32)) {
            LOG_CRITICAL(Debug_Emulated, "debug_buffer_err_code={:X}", Memory::Read32(addr));
        } else {
            // We don't know what's in here so we'll hexdump it
            debug_buffer.resize(sz);
            Memory::ReadBlock(addr, debug_buffer.data(), sz);
            std::string hexdump;
            for (std::size_t i = 0; i < debug_buffer.size(); i++) {
                hexdump += fmt::format("{:02X} ", debug_buffer[i]);
                if (i != 0 && i % 16 == 0) {
                    hexdump += '\n';
                }
            }
            LOG_CRITICAL(Debug_Emulated, "debug_buffer=\n{}", hexdump);
        }
        has_dumped_buffer = true;
    };
    switch (break_reason.break_type) {
    case BreakType::Panic:
        LOG_CRITICAL(Debug_Emulated, "Signalling debugger, PANIC! info1=0x{:016X}, info2=0x{:016X}",
                     info1, info2);
        handle_debug_buffer(info1, info2);
        break;
    case BreakType::AssertionFailed:
        LOG_CRITICAL(Debug_Emulated,
                     "Signalling debugger, Assertion failed! info1=0x{:016X}, info2=0x{:016X}",
                     info1, info2);
        handle_debug_buffer(info1, info2);
        break;
    case BreakType::PreNROLoad:
        LOG_WARNING(
            Debug_Emulated,
            "Signalling debugger, Attempting to load an NRO at 0x{:016X} with size 0x{:016X}",
            info1, info2);
        break;
    case BreakType::PostNROLoad:
        LOG_WARNING(Debug_Emulated,
                    "Signalling debugger, Loaded an NRO at 0x{:016X} with size 0x{:016X}", info1,
                    info2);
        break;
    case BreakType::PreNROUnload:
        LOG_WARNING(
            Debug_Emulated,
            "Signalling debugger, Attempting to unload an NRO at 0x{:016X} with size 0x{:016X}",
            info1, info2);
        break;
    case BreakType::PostNROUnload:
        LOG_WARNING(Debug_Emulated,
                    "Signalling debugger, Unloaded an NRO at 0x{:016X} with size 0x{:016X}", info1,
                    info2);
        break;
    case BreakType::CppException:
        LOG_CRITICAL(Debug_Emulated, "Signalling debugger. Uncaught C++ exception encountered.");
        break;
    default:
        LOG_WARNING(
            Debug_Emulated,
            "Signalling debugger, Unknown break reason {}, info1=0x{:016X}, info2=0x{:016X}",
            static_cast<u32>(break_reason.break_type.Value()), info1, info2);
        handle_debug_buffer(info1, info2);
        break;
    }

    system.GetReporter().SaveSvcBreakReport(
        static_cast<u32>(break_reason.break_type.Value()), break_reason.signal_debugger, info1,
        info2, has_dumped_buffer ? std::make_optional(debug_buffer) : std::nullopt);

    if (!break_reason.signal_debugger) {
        LOG_CRITICAL(
            Debug_Emulated,
            "Emulated program broke execution! reason=0x{:016X}, info1=0x{:016X}, info2=0x{:016X}",
            reason, info1, info2);

        handle_debug_buffer(info1, info2);

        auto* const current_thread = system.CurrentScheduler().GetCurrentThread();
        const auto thread_processor_id = current_thread->GetProcessorID();
        system.ArmInterface(static_cast<std::size_t>(thread_processor_id)).LogBacktrace();
        ASSERT(false);

        system.Kernel().CurrentProcess()->PrepareForTermination();

        // Kill the current thread
        current_thread->Stop();
        system.PrepareReschedule();
    }
}

/// Used to output a message on a debug hardware unit - does nothing on a retail unit
static void OutputDebugString([[maybe_unused]] Core::System& system, VAddr address, u64 len) {
    if (len == 0) {
        return;
    }

    std::string str(len, '\0');
    Memory::ReadBlock(address, str.data(), str.size());
    LOG_DEBUG(Debug_Emulated, "{}", str);
}

/// Gets system/memory information for the current process
static ResultCode GetInfo(Core::System& system, u64* result, u64 info_id, u64 handle,
                          u64 info_sub_id) {
    LOG_TRACE(Kernel_SVC, "called info_id=0x{:X}, info_sub_id=0x{:X}, handle=0x{:08X}", info_id,
              info_sub_id, handle);

    enum class GetInfoType : u64 {
        // 1.0.0+
        AllowedCPUCoreMask = 0,
        AllowedThreadPriorityMask = 1,
        MapRegionBaseAddr = 2,
        MapRegionSize = 3,
        HeapRegionBaseAddr = 4,
        HeapRegionSize = 5,
        TotalPhysicalMemoryAvailable = 6,
        TotalPhysicalMemoryUsed = 7,
        IsCurrentProcessBeingDebugged = 8,
        RegisterResourceLimit = 9,
        IdleTickCount = 10,
        RandomEntropy = 11,
        ThreadTickCount = 0xF0000002,
        // 2.0.0+
        ASLRRegionBaseAddr = 12,
        ASLRRegionSize = 13,
        StackRegionBaseAddr = 14,
        StackRegionSize = 15,
        // 3.0.0+
        SystemResourceSize = 16,
        SystemResourceUsage = 17,
        TitleId = 18,
        // 4.0.0+
        PrivilegedProcessId = 19,
        // 5.0.0+
        UserExceptionContextAddr = 20,
        // 6.0.0+
        TotalPhysicalMemoryAvailableWithoutSystemResource = 21,
        TotalPhysicalMemoryUsedWithoutSystemResource = 22,
    };

    const auto info_id_type = static_cast<GetInfoType>(info_id);

    switch (info_id_type) {
    case GetInfoType::AllowedCPUCoreMask:
    case GetInfoType::AllowedThreadPriorityMask:
    case GetInfoType::MapRegionBaseAddr:
    case GetInfoType::MapRegionSize:
    case GetInfoType::HeapRegionBaseAddr:
    case GetInfoType::HeapRegionSize:
    case GetInfoType::ASLRRegionBaseAddr:
    case GetInfoType::ASLRRegionSize:
    case GetInfoType::StackRegionBaseAddr:
    case GetInfoType::StackRegionSize:
    case GetInfoType::TotalPhysicalMemoryAvailable:
    case GetInfoType::TotalPhysicalMemoryUsed:
    case GetInfoType::SystemResourceSize:
    case GetInfoType::SystemResourceUsage:
    case GetInfoType::TitleId:
    case GetInfoType::UserExceptionContextAddr:
    case GetInfoType::TotalPhysicalMemoryAvailableWithoutSystemResource:
    case GetInfoType::TotalPhysicalMemoryUsedWithoutSystemResource: {
        if (info_sub_id != 0) {
            return ERR_INVALID_ENUM_VALUE;
        }

        const auto& current_process_handle_table =
            system.Kernel().CurrentProcess()->GetHandleTable();
        const auto process = current_process_handle_table.Get<Process>(static_cast<Handle>(handle));
        if (!process) {
            return ERR_INVALID_HANDLE;
        }

        switch (info_id_type) {
        case GetInfoType::AllowedCPUCoreMask:
            *result = process->GetCoreMask();
            return RESULT_SUCCESS;

        case GetInfoType::AllowedThreadPriorityMask:
            *result = process->GetPriorityMask();
            return RESULT_SUCCESS;

        case GetInfoType::MapRegionBaseAddr:
            *result = process->VMManager().GetMapRegionBaseAddress();
            return RESULT_SUCCESS;

        case GetInfoType::MapRegionSize:
            *result = process->VMManager().GetMapRegionSize();
            return RESULT_SUCCESS;

        case GetInfoType::HeapRegionBaseAddr:
            *result = process->VMManager().GetHeapRegionBaseAddress();
            return RESULT_SUCCESS;

        case GetInfoType::HeapRegionSize:
            *result = process->VMManager().GetHeapRegionSize();
            return RESULT_SUCCESS;

        case GetInfoType::ASLRRegionBaseAddr:
            *result = process->VMManager().GetASLRRegionBaseAddress();
            return RESULT_SUCCESS;

        case GetInfoType::ASLRRegionSize:
            *result = process->VMManager().GetASLRRegionSize();
            return RESULT_SUCCESS;

        case GetInfoType::StackRegionBaseAddr:
            *result = process->VMManager().GetStackRegionBaseAddress();
            return RESULT_SUCCESS;

        case GetInfoType::StackRegionSize:
            *result = process->VMManager().GetStackRegionSize();
            return RESULT_SUCCESS;

        case GetInfoType::TotalPhysicalMemoryAvailable:
            *result = process->GetTotalPhysicalMemoryAvailable();
            return RESULT_SUCCESS;

        case GetInfoType::TotalPhysicalMemoryUsed:
            *result = process->GetTotalPhysicalMemoryUsed();
            return RESULT_SUCCESS;

        case GetInfoType::SystemResourceSize:
            *result = process->GetSystemResourceSize();
            return RESULT_SUCCESS;

        case GetInfoType::SystemResourceUsage:
            LOG_WARNING(Kernel_SVC, "(STUBBED) Attempted to query system resource usage");
            *result = process->GetSystemResourceUsage();
            return RESULT_SUCCESS;

        case GetInfoType::TitleId:
            *result = process->GetTitleID();
            return RESULT_SUCCESS;

        case GetInfoType::UserExceptionContextAddr:
            *result = process->GetTLSRegionAddress();
            return RESULT_SUCCESS;

        case GetInfoType::TotalPhysicalMemoryAvailableWithoutSystemResource:
            *result = process->GetTotalPhysicalMemoryAvailableWithoutSystemResource();
            return RESULT_SUCCESS;

        case GetInfoType::TotalPhysicalMemoryUsedWithoutSystemResource:
            *result = process->GetTotalPhysicalMemoryUsedWithoutSystemResource();
            return RESULT_SUCCESS;

        default:
            break;
        }

        LOG_WARNING(Kernel_SVC, "(STUBBED) Unimplemented svcGetInfo id=0x{:016X}", info_id);
        return ERR_INVALID_ENUM_VALUE;
    }

    case GetInfoType::IsCurrentProcessBeingDebugged:
        *result = 0;
        return RESULT_SUCCESS;

    case GetInfoType::RegisterResourceLimit: {
        if (handle != 0) {
            return ERR_INVALID_HANDLE;
        }

        if (info_sub_id != 0) {
            return ERR_INVALID_COMBINATION;
        }

        Process* const current_process = system.Kernel().CurrentProcess();
        HandleTable& handle_table = current_process->GetHandleTable();
        const auto resource_limit = current_process->GetResourceLimit();
        if (!resource_limit) {
            *result = KernelHandle::InvalidHandle;
            // Yes, the kernel considers this a successful operation.
            return RESULT_SUCCESS;
        }

        const auto table_result = handle_table.Create(resource_limit);
        if (table_result.Failed()) {
            return table_result.Code();
        }

        *result = *table_result;
        return RESULT_SUCCESS;
    }

    case GetInfoType::RandomEntropy:
        if (handle != 0) {
            LOG_ERROR(Kernel_SVC, "Process Handle is non zero, expected 0 result but got {:016X}",
                      handle);
            return ERR_INVALID_HANDLE;
        }

        if (info_sub_id >= Process::RANDOM_ENTROPY_SIZE) {
            LOG_ERROR(Kernel_SVC, "Entropy size is out of range, expected {} but got {}",
                      Process::RANDOM_ENTROPY_SIZE, info_sub_id);
            return ERR_INVALID_COMBINATION;
        }

        *result = system.Kernel().CurrentProcess()->GetRandomEntropy(info_sub_id);
        return RESULT_SUCCESS;

    case GetInfoType::PrivilegedProcessId:
        LOG_WARNING(Kernel_SVC,
                    "(STUBBED) Attempted to query privileged process id bounds, returned 0");
        *result = 0;
        return RESULT_SUCCESS;

    case GetInfoType::ThreadTickCount: {
        constexpr u64 num_cpus = 4;
        if (info_sub_id != 0xFFFFFFFFFFFFFFFF && info_sub_id >= num_cpus) {
            LOG_ERROR(Kernel_SVC, "Core count is out of range, expected {} but got {}", num_cpus,
                      info_sub_id);
            return ERR_INVALID_COMBINATION;
        }

        const auto thread = system.Kernel().CurrentProcess()->GetHandleTable().Get<Thread>(
            static_cast<Handle>(handle));
        if (!thread) {
            LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}",
                      static_cast<Handle>(handle));
            return ERR_INVALID_HANDLE;
        }

        const auto& core_timing = system.CoreTiming();
        const auto& scheduler = system.CurrentScheduler();
        const auto* const current_thread = scheduler.GetCurrentThread();
        const bool same_thread = current_thread == thread;

        const u64 prev_ctx_ticks = scheduler.GetLastContextSwitchTicks();
        u64 out_ticks = 0;
        if (same_thread && info_sub_id == 0xFFFFFFFFFFFFFFFF) {
            const u64 thread_ticks = current_thread->GetTotalCPUTimeTicks();

            out_ticks = thread_ticks + (core_timing.GetTicks() - prev_ctx_ticks);
        } else if (same_thread && info_sub_id == system.CurrentCoreIndex()) {
            out_ticks = core_timing.GetTicks() - prev_ctx_ticks;
        }

        *result = out_ticks;
        return RESULT_SUCCESS;
    }

    default:
        LOG_WARNING(Kernel_SVC, "(STUBBED) Unimplemented svcGetInfo id=0x{:016X}", info_id);
        return ERR_INVALID_ENUM_VALUE;
    }
}

/// Maps memory at a desired address
static ResultCode MapPhysicalMemory(Core::System& system, VAddr addr, u64 size) {
    LOG_DEBUG(Kernel_SVC, "called, addr=0x{:016X}, size=0x{:X}", addr, size);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address is not aligned to 4KB, 0x{:016X}", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, 0x{:X}", size);
        return ERR_INVALID_SIZE;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is zero");
        return ERR_INVALID_SIZE;
    }

    if (!(addr < addr + size)) {
        LOG_ERROR(Kernel_SVC, "Size causes 64-bit overflow of address");
        return ERR_INVALID_MEMORY_RANGE;
    }

    Process* const current_process = system.Kernel().CurrentProcess();
    auto& vm_manager = current_process->VMManager();

    if (current_process->GetSystemResourceSize() == 0) {
        LOG_ERROR(Kernel_SVC, "System Resource Size is zero");
        return ERR_INVALID_STATE;
    }

    if (!vm_manager.IsWithinMapRegion(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Range not within map region");
        return ERR_INVALID_MEMORY_RANGE;
    }

    return vm_manager.MapPhysicalMemory(addr, size);
}

/// Unmaps memory previously mapped via MapPhysicalMemory
static ResultCode UnmapPhysicalMemory(Core::System& system, VAddr addr, u64 size) {
    LOG_DEBUG(Kernel_SVC, "called, addr=0x{:016X}, size=0x{:X}", addr, size);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address is not aligned to 4KB, 0x{:016X}", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, 0x{:X}", size);
        return ERR_INVALID_SIZE;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is zero");
        return ERR_INVALID_SIZE;
    }

    if (!(addr < addr + size)) {
        LOG_ERROR(Kernel_SVC, "Size causes 64-bit overflow of address");
        return ERR_INVALID_MEMORY_RANGE;
    }

    Process* const current_process = system.Kernel().CurrentProcess();
    auto& vm_manager = current_process->VMManager();

    if (current_process->GetSystemResourceSize() == 0) {
        LOG_ERROR(Kernel_SVC, "System Resource Size is zero");
        return ERR_INVALID_STATE;
    }

    if (!vm_manager.IsWithinMapRegion(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Range not within map region");
        return ERR_INVALID_MEMORY_RANGE;
    }

    return vm_manager.UnmapPhysicalMemory(addr, size);
}

/// Sets the thread activity
static ResultCode SetThreadActivity(Core::System& system, Handle handle, u32 activity) {
    LOG_DEBUG(Kernel_SVC, "called, handle=0x{:08X}, activity=0x{:08X}", handle, activity);
    if (activity > static_cast<u32>(ThreadActivity::Paused)) {
        return ERR_INVALID_ENUM_VALUE;
    }

    const auto* current_process = system.Kernel().CurrentProcess();
    const SharedPtr<Thread> thread = current_process->GetHandleTable().Get<Thread>(handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}", handle);
        return ERR_INVALID_HANDLE;
    }

    if (thread->GetOwnerProcess() != current_process) {
        LOG_ERROR(Kernel_SVC,
                  "The current process does not own the current thread, thread_handle={:08X} "
                  "thread_pid={}, "
                  "current_process_pid={}",
                  handle, thread->GetOwnerProcess()->GetProcessID(),
                  current_process->GetProcessID());
        return ERR_INVALID_HANDLE;
    }

    if (thread == system.CurrentScheduler().GetCurrentThread()) {
        LOG_ERROR(Kernel_SVC, "The thread handle specified is the current running thread");
        return ERR_BUSY;
    }

    thread->SetActivity(static_cast<ThreadActivity>(activity));

    system.PrepareReschedule(thread->GetProcessorID());
    return RESULT_SUCCESS;
}

/// Gets the thread context
static ResultCode GetThreadContext(Core::System& system, VAddr thread_context, Handle handle) {
    LOG_DEBUG(Kernel_SVC, "called, context=0x{:08X}, thread=0x{:X}", thread_context, handle);

    const auto* current_process = system.Kernel().CurrentProcess();
    const SharedPtr<Thread> thread = current_process->GetHandleTable().Get<Thread>(handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}", handle);
        return ERR_INVALID_HANDLE;
    }

    if (thread->GetOwnerProcess() != current_process) {
        LOG_ERROR(Kernel_SVC,
                  "The current process does not own the current thread, thread_handle={:08X} "
                  "thread_pid={}, "
                  "current_process_pid={}",
                  handle, thread->GetOwnerProcess()->GetProcessID(),
                  current_process->GetProcessID());
        return ERR_INVALID_HANDLE;
    }

    if (thread == system.CurrentScheduler().GetCurrentThread()) {
        LOG_ERROR(Kernel_SVC, "The thread handle specified is the current running thread");
        return ERR_BUSY;
    }

    Core::ARM_Interface::ThreadContext ctx = thread->GetContext();
    // Mask away mode bits, interrupt bits, IL bit, and other reserved bits.
    ctx.pstate &= 0xFF0FFE20;

    // If 64-bit, we can just write the context registers directly and we're good.
    // However, if 32-bit, we have to ensure some registers are zeroed out.
    if (!current_process->Is64BitProcess()) {
        std::fill(ctx.cpu_registers.begin() + 15, ctx.cpu_registers.end(), 0);
        std::fill(ctx.vector_registers.begin() + 16, ctx.vector_registers.end(), u128{});
    }

    Memory::WriteBlock(thread_context, &ctx, sizeof(ctx));
    return RESULT_SUCCESS;
}

/// Gets the priority for the specified thread
static ResultCode GetThreadPriority(Core::System& system, u32* priority, Handle handle) {
    LOG_TRACE(Kernel_SVC, "called");

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const SharedPtr<Thread> thread = handle_table.Get<Thread>(handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}", handle);
        return ERR_INVALID_HANDLE;
    }

    *priority = thread->GetPriority();
    return RESULT_SUCCESS;
}

/// Sets the priority for the specified thread
static ResultCode SetThreadPriority(Core::System& system, Handle handle, u32 priority) {
    LOG_TRACE(Kernel_SVC, "called");

    if (priority > THREADPRIO_LOWEST) {
        LOG_ERROR(
            Kernel_SVC,
            "An invalid priority was specified, expected {} but got {} for thread_handle={:08X}",
            THREADPRIO_LOWEST, priority, handle);
        return ERR_INVALID_THREAD_PRIORITY;
    }

    const auto* const current_process = system.Kernel().CurrentProcess();

    SharedPtr<Thread> thread = current_process->GetHandleTable().Get<Thread>(handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, handle=0x{:08X}", handle);
        return ERR_INVALID_HANDLE;
    }

    thread->SetPriority(priority);

    system.PrepareReschedule(thread->GetProcessorID());
    return RESULT_SUCCESS;
}

/// Get which CPU core is executing the current thread
static u32 GetCurrentProcessorNumber(Core::System& system) {
    LOG_TRACE(Kernel_SVC, "called");
    return system.CurrentScheduler().GetCurrentThread()->GetProcessorID();
}

static ResultCode MapSharedMemory(Core::System& system, Handle shared_memory_handle, VAddr addr,
                                  u64 size, u32 permissions) {
    LOG_TRACE(Kernel_SVC,
              "called, shared_memory_handle=0x{:X}, addr=0x{:X}, size=0x{:X}, permissions=0x{:08X}",
              shared_memory_handle, addr, size, permissions);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address is not aligned to 4KB, addr=0x{:016X}", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        return ERR_INVALID_SIZE;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, size=0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Region is not a valid address range, addr=0x{:016X}, size=0x{:016X}",
                  addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto permissions_type = static_cast<MemoryPermission>(permissions);
    if (permissions_type != MemoryPermission::Read &&
        permissions_type != MemoryPermission::ReadWrite) {
        LOG_ERROR(Kernel_SVC, "Expected Read or ReadWrite permission but got permissions=0x{:08X}",
                  permissions);
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    auto shared_memory = current_process->GetHandleTable().Get<SharedMemory>(shared_memory_handle);
    if (!shared_memory) {
        LOG_ERROR(Kernel_SVC, "Shared memory does not exist, shared_memory_handle=0x{:08X}",
                  shared_memory_handle);
        return ERR_INVALID_HANDLE;
    }

    const auto& vm_manager = current_process->VMManager();
    if (!vm_manager.IsWithinASLRRegion(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Region is not within the ASLR region. addr=0x{:016X}, size={:016X}",
                  addr, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return shared_memory->Map(*current_process, addr, permissions_type, MemoryPermission::DontCare);
}

static ResultCode UnmapSharedMemory(Core::System& system, Handle shared_memory_handle, VAddr addr,
                                    u64 size) {
    LOG_WARNING(Kernel_SVC, "called, shared_memory_handle=0x{:08X}, addr=0x{:X}, size=0x{:X}",
                shared_memory_handle, addr, size);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address is not aligned to 4KB, addr=0x{:016X}", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        return ERR_INVALID_SIZE;
    }

    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, size=0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Region is not a valid address range, addr=0x{:016X}, size=0x{:016X}",
                  addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    auto shared_memory = current_process->GetHandleTable().Get<SharedMemory>(shared_memory_handle);
    if (!shared_memory) {
        LOG_ERROR(Kernel_SVC, "Shared memory does not exist, shared_memory_handle=0x{:08X}",
                  shared_memory_handle);
        return ERR_INVALID_HANDLE;
    }

    const auto& vm_manager = current_process->VMManager();
    if (!vm_manager.IsWithinASLRRegion(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Region is not within the ASLR region. addr=0x{:016X}, size={:016X}",
                  addr, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return shared_memory->Unmap(*current_process, addr, size);
}

static ResultCode QueryProcessMemory(Core::System& system, VAddr memory_info_address,
                                     VAddr page_info_address, Handle process_handle,
                                     VAddr address) {
    LOG_TRACE(Kernel_SVC, "called process=0x{:08X} address={:X}", process_handle, address);
    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    SharedPtr<Process> process = handle_table.Get<Process>(process_handle);
    if (!process) {
        LOG_ERROR(Kernel_SVC, "Process handle does not exist, process_handle=0x{:08X}",
                  process_handle);
        return ERR_INVALID_HANDLE;
    }

    const auto& vm_manager = process->VMManager();
    const MemoryInfo memory_info = vm_manager.QueryMemory(address);

    Memory::Write64(memory_info_address, memory_info.base_address);
    Memory::Write64(memory_info_address + 8, memory_info.size);
    Memory::Write32(memory_info_address + 16, memory_info.state);
    Memory::Write32(memory_info_address + 20, memory_info.attributes);
    Memory::Write32(memory_info_address + 24, memory_info.permission);
    Memory::Write32(memory_info_address + 32, memory_info.ipc_ref_count);
    Memory::Write32(memory_info_address + 28, memory_info.device_ref_count);
    Memory::Write32(memory_info_address + 36, 0);

    // Page info appears to be currently unused by the kernel and is always set to zero.
    Memory::Write32(page_info_address, 0);

    return RESULT_SUCCESS;
}

static ResultCode QueryMemory(Core::System& system, VAddr memory_info_address,
                              VAddr page_info_address, VAddr query_address) {
    LOG_TRACE(Kernel_SVC,
              "called, memory_info_address=0x{:016X}, page_info_address=0x{:016X}, "
              "query_address=0x{:016X}",
              memory_info_address, page_info_address, query_address);

    return QueryProcessMemory(system, memory_info_address, page_info_address, CurrentProcess,
                              query_address);
}

static ResultCode MapProcessCodeMemory(Core::System& system, Handle process_handle, u64 dst_address,
                                       u64 src_address, u64 size) {
    LOG_DEBUG(Kernel_SVC,
              "called. process_handle=0x{:08X}, dst_address=0x{:016X}, "
              "src_address=0x{:016X}, size=0x{:016X}",
              process_handle, dst_address, src_address, size);

    if (!Common::Is4KBAligned(src_address)) {
        LOG_ERROR(Kernel_SVC, "src_address is not page-aligned (src_address=0x{:016X}).",
                  src_address);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(dst_address)) {
        LOG_ERROR(Kernel_SVC, "dst_address is not page-aligned (dst_address=0x{:016X}).",
                  dst_address);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0 || !Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is zero or not page-aligned (size=0x{:016X})", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(dst_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination address range overflows the address space (dst_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  dst_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!IsValidAddressRange(src_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source address range overflows the address space (src_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  src_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    auto process = handle_table.Get<Process>(process_handle);
    if (!process) {
        LOG_ERROR(Kernel_SVC, "Invalid process handle specified (handle=0x{:08X}).",
                  process_handle);
        return ERR_INVALID_HANDLE;
    }

    auto& vm_manager = process->VMManager();
    if (!vm_manager.IsWithinAddressSpace(src_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source address range is not within the address space (src_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  src_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!vm_manager.IsWithinASLRRegion(dst_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination address range is not within the ASLR region (dst_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  dst_address, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return vm_manager.MapCodeMemory(dst_address, src_address, size);
}

static ResultCode UnmapProcessCodeMemory(Core::System& system, Handle process_handle,
                                         u64 dst_address, u64 src_address, u64 size) {
    LOG_DEBUG(Kernel_SVC,
              "called. process_handle=0x{:08X}, dst_address=0x{:016X}, src_address=0x{:016X}, "
              "size=0x{:016X}",
              process_handle, dst_address, src_address, size);

    if (!Common::Is4KBAligned(dst_address)) {
        LOG_ERROR(Kernel_SVC, "dst_address is not page-aligned (dst_address=0x{:016X}).",
                  dst_address);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(src_address)) {
        LOG_ERROR(Kernel_SVC, "src_address is not page-aligned (src_address=0x{:016X}).",
                  src_address);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0 || Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is zero or not page-aligned (size=0x{:016X}).", size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(dst_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination address range overflows the address space (dst_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  dst_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!IsValidAddressRange(src_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source address range overflows the address space (src_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  src_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    auto process = handle_table.Get<Process>(process_handle);
    if (!process) {
        LOG_ERROR(Kernel_SVC, "Invalid process handle specified (handle=0x{:08X}).",
                  process_handle);
        return ERR_INVALID_HANDLE;
    }

    auto& vm_manager = process->VMManager();
    if (!vm_manager.IsWithinAddressSpace(src_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source address range is not within the address space (src_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  src_address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!vm_manager.IsWithinASLRRegion(dst_address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination address range is not within the ASLR region (dst_address=0x{:016X}, "
                  "size=0x{:016X}).",
                  dst_address, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return vm_manager.UnmapCodeMemory(dst_address, src_address, size);
}

/// Exits the current process
static void ExitProcess(Core::System& system) {
    auto* current_process = system.Kernel().CurrentProcess();

    LOG_INFO(Kernel_SVC, "Process {} exiting", current_process->GetProcessID());
    ASSERT_MSG(current_process->GetStatus() == ProcessStatus::Running,
               "Process has already exited");

    current_process->PrepareForTermination();

    // Kill the current thread
    system.CurrentScheduler().GetCurrentThread()->Stop();

    system.PrepareReschedule();
}

/// Creates a new thread
static ResultCode CreateThread(Core::System& system, Handle* out_handle, VAddr entry_point, u64 arg,
                               VAddr stack_top, u32 priority, s32 processor_id) {
    LOG_DEBUG(Kernel_SVC,
              "called entrypoint=0x{:08X}, arg=0x{:08X}, stacktop=0x{:08X}, "
              "threadpriority=0x{:08X}, processorid=0x{:08X} : created handle=0x{:08X}",
              entry_point, arg, stack_top, priority, processor_id, *out_handle);

    auto* const current_process = system.Kernel().CurrentProcess();

    if (processor_id == THREADPROCESSORID_IDEAL) {
        // Set the target CPU to the one specified by the process.
        processor_id = current_process->GetIdealCore();
        ASSERT(processor_id != THREADPROCESSORID_IDEAL);
    }

    if (processor_id < THREADPROCESSORID_0 || processor_id > THREADPROCESSORID_3) {
        LOG_ERROR(Kernel_SVC, "Invalid thread processor ID: {}", processor_id);
        return ERR_INVALID_PROCESSOR_ID;
    }

    const u64 core_mask = current_process->GetCoreMask();
    if ((core_mask | (1ULL << processor_id)) != core_mask) {
        LOG_ERROR(Kernel_SVC, "Invalid thread core specified ({})", processor_id);
        return ERR_INVALID_PROCESSOR_ID;
    }

    if (priority > THREADPRIO_LOWEST) {
        LOG_ERROR(Kernel_SVC,
                  "Invalid thread priority specified ({}). Must be within the range 0-64",
                  priority);
        return ERR_INVALID_THREAD_PRIORITY;
    }

    if (((1ULL << priority) & current_process->GetPriorityMask()) == 0) {
        LOG_ERROR(Kernel_SVC, "Invalid thread priority specified ({})", priority);
        return ERR_INVALID_THREAD_PRIORITY;
    }

    auto& kernel = system.Kernel();
    CASCADE_RESULT(SharedPtr<Thread> thread,
                   Thread::Create(kernel, "", entry_point, priority, arg, processor_id, stack_top,
                                  *current_process));

    const auto new_thread_handle = current_process->GetHandleTable().Create(thread);
    if (new_thread_handle.Failed()) {
        LOG_ERROR(Kernel_SVC, "Failed to create handle with error=0x{:X}",
                  new_thread_handle.Code().raw);
        return new_thread_handle.Code();
    }
    *out_handle = *new_thread_handle;

    // Set the thread name for debugging purposes.
    thread->SetName(
        fmt::format("thread[entry_point={:X}, handle={:X}]", entry_point, *new_thread_handle));

    system.PrepareReschedule(thread->GetProcessorID());

    return RESULT_SUCCESS;
}

/// Starts the thread for the provided handle
static ResultCode StartThread(Core::System& system, Handle thread_handle) {
    LOG_DEBUG(Kernel_SVC, "called thread=0x{:08X}", thread_handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, thread_handle=0x{:08X}",
                  thread_handle);
        return ERR_INVALID_HANDLE;
    }

    ASSERT(thread->GetStatus() == ThreadStatus::Dormant);

    thread->ResumeFromWait();

    if (thread->GetStatus() == ThreadStatus::Ready) {
        system.PrepareReschedule(thread->GetProcessorID());
    }

    return RESULT_SUCCESS;
}

/// Called when a thread exits
static void ExitThread(Core::System& system) {
    LOG_DEBUG(Kernel_SVC, "called, pc=0x{:08X}", system.CurrentArmInterface().GetPC());

    auto* const current_thread = system.CurrentScheduler().GetCurrentThread();
    current_thread->Stop();
    system.GlobalScheduler().RemoveThread(current_thread);
    system.PrepareReschedule();
}

/// Sleep the current thread
static void SleepThread(Core::System& system, s64 nanoseconds) {
    LOG_DEBUG(Kernel_SVC, "called nanoseconds={}", nanoseconds);

    enum class SleepType : s64 {
        YieldWithoutLoadBalancing = 0,
        YieldWithLoadBalancing = -1,
        YieldAndWaitForLoadBalancing = -2,
    };

    auto& scheduler = system.CurrentScheduler();
    auto* const current_thread = scheduler.GetCurrentThread();
    bool is_redundant = false;

    if (nanoseconds <= 0) {
        switch (static_cast<SleepType>(nanoseconds)) {
        case SleepType::YieldWithoutLoadBalancing:
            is_redundant = current_thread->YieldSimple();
            break;
        case SleepType::YieldWithLoadBalancing:
            is_redundant = current_thread->YieldAndBalanceLoad();
            break;
        case SleepType::YieldAndWaitForLoadBalancing:
            is_redundant = current_thread->YieldAndWaitForLoadBalancing();
            break;
        default:
            UNREACHABLE_MSG("Unimplemented sleep yield type '{:016X}'!", nanoseconds);
        }
    } else {
        current_thread->Sleep(nanoseconds);
    }

    if (is_redundant) {
        // If it's redundant, the core is pretty much idle. Some games keep idling
        // a core while it's doing nothing, we advance timing to avoid costly continuous
        // calls.
        system.CoreTiming().AddTicks(2000);
    }
    system.PrepareReschedule(current_thread->GetProcessorID());
}

/// Wait process wide key atomic
static ResultCode WaitProcessWideKeyAtomic(Core::System& system, VAddr mutex_addr,
                                           VAddr condition_variable_addr, Handle thread_handle,
                                           s64 nano_seconds) {
    LOG_TRACE(
        Kernel_SVC,
        "called mutex_addr={:X}, condition_variable_addr={:X}, thread_handle=0x{:08X}, timeout={}",
        mutex_addr, condition_variable_addr, thread_handle, nano_seconds);

    if (Memory::IsKernelVirtualAddress(mutex_addr)) {
        LOG_ERROR(
            Kernel_SVC,
            "Given mutex address must not be within the kernel address space. address=0x{:016X}",
            mutex_addr);
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (!Common::IsWordAligned(mutex_addr)) {
        LOG_ERROR(Kernel_SVC, "Given mutex address must be word-aligned. address=0x{:016X}",
                  mutex_addr);
        return ERR_INVALID_ADDRESS;
    }

    ASSERT(condition_variable_addr == Common::AlignDown(condition_variable_addr, 4));

    auto* const current_process = system.Kernel().CurrentProcess();
    const auto& handle_table = current_process->GetHandleTable();
    SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    ASSERT(thread);

    SharedPtr<Thread> current_thread = system.CurrentScheduler().GetCurrentThread();

    const auto release_result =
        current_process->GetMutex().Release(mutex_addr, current_thread.get());
    if (release_result.IsError()) {
        return release_result;
    }

    current_thread->SetCondVarWaitAddress(condition_variable_addr);
    current_thread->SetMutexWaitAddress(mutex_addr);
    current_thread->SetWaitHandle(thread_handle);
    current_thread->SetStatus(ThreadStatus::WaitCondVar);
    current_thread->InvalidateWakeupCallback();

    current_thread->WakeAfterDelay(nano_seconds);

    // Note: Deliberately don't attempt to inherit the lock owner's priority.

    system.PrepareReschedule(current_thread->GetProcessorID());
    return RESULT_SUCCESS;
}

/// Signal process wide key
static ResultCode SignalProcessWideKey(Core::System& system, VAddr condition_variable_addr,
                                       s32 target) {
    LOG_TRACE(Kernel_SVC, "called, condition_variable_addr=0x{:X}, target=0x{:08X}",
              condition_variable_addr, target);

    ASSERT(condition_variable_addr == Common::AlignDown(condition_variable_addr, 4));

    // Retrieve a list of all threads that are waiting for this condition variable.
    std::vector<SharedPtr<Thread>> waiting_threads;
    const auto& scheduler = system.GlobalScheduler();
    const auto& thread_list = scheduler.GetThreadList();

    for (const auto& thread : thread_list) {
        if (thread->GetCondVarWaitAddress() == condition_variable_addr) {
            waiting_threads.push_back(thread);
        }
    }

    // Sort them by priority, such that the highest priority ones come first.
    std::sort(waiting_threads.begin(), waiting_threads.end(),
              [](const SharedPtr<Thread>& lhs, const SharedPtr<Thread>& rhs) {
                  return lhs->GetPriority() < rhs->GetPriority();
              });

    // Only process up to 'target' threads, unless 'target' is -1, in which case process
    // them all.
    std::size_t last = waiting_threads.size();
    if (target != -1)
        last = std::min(waiting_threads.size(), static_cast<std::size_t>(target));

    // If there are no threads waiting on this condition variable, just exit
    if (last == 0)
        return RESULT_SUCCESS;

    for (std::size_t index = 0; index < last; ++index) {
        auto& thread = waiting_threads[index];

        ASSERT(thread->GetCondVarWaitAddress() == condition_variable_addr);

        // liberate Cond Var Thread.
        thread->SetCondVarWaitAddress(0);

        const std::size_t current_core = system.CurrentCoreIndex();
        auto& monitor = system.Monitor();

        // Atomically read the value of the mutex.
        u32 mutex_val = 0;
        u32 update_val = 0;
        const VAddr mutex_address = thread->GetMutexWaitAddress();
        do {
            monitor.SetExclusive(current_core, mutex_address);

            // If the mutex is not yet acquired, acquire it.
            mutex_val = Memory::Read32(mutex_address);

            if (mutex_val != 0) {
                update_val = mutex_val | Mutex::MutexHasWaitersFlag;
            } else {
                update_val = thread->GetWaitHandle();
            }
        } while (!monitor.ExclusiveWrite32(current_core, mutex_address, update_val));
        if (mutex_val == 0) {
            // We were able to acquire the mutex, resume this thread.
            ASSERT(thread->GetStatus() == ThreadStatus::WaitCondVar);
            thread->ResumeFromWait();

            auto* const lock_owner = thread->GetLockOwner();
            if (lock_owner != nullptr) {
                lock_owner->RemoveMutexWaiter(thread);
            }

            thread->SetLockOwner(nullptr);
            thread->SetMutexWaitAddress(0);
            thread->SetWaitHandle(0);
            thread->SetWaitSynchronizationResult(RESULT_SUCCESS);
            system.PrepareReschedule(thread->GetProcessorID());
        } else {
            // The mutex is already owned by some other thread, make this thread wait on it.
            const Handle owner_handle = static_cast<Handle>(mutex_val & Mutex::MutexOwnerMask);
            const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
            auto owner = handle_table.Get<Thread>(owner_handle);
            ASSERT(owner);
            ASSERT(thread->GetStatus() == ThreadStatus::WaitCondVar);
            thread->InvalidateWakeupCallback();
            thread->SetStatus(ThreadStatus::WaitMutex);

            owner->AddMutexWaiter(thread);
            system.PrepareReschedule(thread->GetProcessorID());
        }
    }

    return RESULT_SUCCESS;
}

// Wait for an address (via Address Arbiter)
static ResultCode WaitForAddress(Core::System& system, VAddr address, u32 type, s32 value,
                                 s64 timeout) {
    LOG_TRACE(Kernel_SVC, "called, address=0x{:X}, type=0x{:X}, value=0x{:X}, timeout={}", address,
              type, value, timeout);

    // If the passed address is a kernel virtual address, return invalid memory state.
    if (Memory::IsKernelVirtualAddress(address)) {
        LOG_ERROR(Kernel_SVC, "Address is a kernel virtual address, address={:016X}", address);
        return ERR_INVALID_ADDRESS_STATE;
    }

    // If the address is not properly aligned to 4 bytes, return invalid address.
    if (!Common::IsWordAligned(address)) {
        LOG_ERROR(Kernel_SVC, "Address is not word aligned, address={:016X}", address);
        return ERR_INVALID_ADDRESS;
    }

    const auto arbitration_type = static_cast<AddressArbiter::ArbitrationType>(type);
    auto& address_arbiter = system.Kernel().CurrentProcess()->GetAddressArbiter();
    const ResultCode result =
        address_arbiter.WaitForAddress(address, arbitration_type, value, timeout);
    if (result == RESULT_SUCCESS) {
        system.PrepareReschedule();
    }
    return result;
}

// Signals to an address (via Address Arbiter)
static ResultCode SignalToAddress(Core::System& system, VAddr address, u32 type, s32 value,
                                  s32 num_to_wake) {
    LOG_TRACE(Kernel_SVC, "called, address=0x{:X}, type=0x{:X}, value=0x{:X}, num_to_wake=0x{:X}",
              address, type, value, num_to_wake);

    // If the passed address is a kernel virtual address, return invalid memory state.
    if (Memory::IsKernelVirtualAddress(address)) {
        LOG_ERROR(Kernel_SVC, "Address is a kernel virtual address, address={:016X}", address);
        return ERR_INVALID_ADDRESS_STATE;
    }

    // If the address is not properly aligned to 4 bytes, return invalid address.
    if (!Common::IsWordAligned(address)) {
        LOG_ERROR(Kernel_SVC, "Address is not word aligned, address={:016X}", address);
        return ERR_INVALID_ADDRESS;
    }

    const auto signal_type = static_cast<AddressArbiter::SignalType>(type);
    auto& address_arbiter = system.Kernel().CurrentProcess()->GetAddressArbiter();
    return address_arbiter.SignalToAddress(address, signal_type, value, num_to_wake);
}

/// This returns the total CPU ticks elapsed since the CPU was powered-on
static u64 GetSystemTick(Core::System& system) {
    LOG_TRACE(Kernel_SVC, "called");

    auto& core_timing = system.CoreTiming();
    const u64 result{core_timing.GetTicks()};

    // Advance time to defeat dumb games that busy-wait for the frame to end.
    core_timing.AddTicks(400);

    return result;
}

/// Close a handle
static ResultCode CloseHandle(Core::System& system, Handle handle) {
    LOG_TRACE(Kernel_SVC, "Closing handle 0x{:08X}", handle);

    auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    return handle_table.Close(handle);
}

/// Clears the signaled state of an event or process.
static ResultCode ResetSignal(Core::System& system, Handle handle) {
    LOG_DEBUG(Kernel_SVC, "called handle 0x{:08X}", handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();

    auto event = handle_table.Get<ReadableEvent>(handle);
    if (event) {
        return event->Reset();
    }

    auto process = handle_table.Get<Process>(handle);
    if (process) {
        return process->ClearSignalState();
    }

    LOG_ERROR(Kernel_SVC, "Invalid handle (0x{:08X})", handle);
    return ERR_INVALID_HANDLE;
}

/// Creates a TransferMemory object
static ResultCode CreateTransferMemory(Core::System& system, Handle* handle, VAddr addr, u64 size,
                                       u32 permissions) {
    LOG_DEBUG(Kernel_SVC, "called addr=0x{:X}, size=0x{:X}, perms=0x{:08X}", addr, size,
              permissions);

    if (!Common::Is4KBAligned(addr)) {
        LOG_ERROR(Kernel_SVC, "Address ({:016X}) is not page aligned!", addr);
        return ERR_INVALID_ADDRESS;
    }

    if (!Common::Is4KBAligned(size) || size == 0) {
        LOG_ERROR(Kernel_SVC, "Size ({:016X}) is not page aligned or equal to zero!", size);
        return ERR_INVALID_ADDRESS;
    }

    if (!IsValidAddressRange(addr, size)) {
        LOG_ERROR(Kernel_SVC, "Address and size cause overflow! (address={:016X}, size={:016X})",
                  addr, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto perms = static_cast<MemoryPermission>(permissions);
    if (perms != MemoryPermission::None && perms != MemoryPermission::Read &&
        perms != MemoryPermission::ReadWrite) {
        LOG_ERROR(Kernel_SVC, "Invalid memory permissions for transfer memory! (perms={:08X})",
                  permissions);
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    auto& kernel = system.Kernel();
    auto transfer_mem_handle = TransferMemory::Create(kernel, addr, size, perms);

    auto& handle_table = kernel.CurrentProcess()->GetHandleTable();
    const auto result = handle_table.Create(std::move(transfer_mem_handle));
    if (result.Failed()) {
        return result.Code();
    }

    *handle = *result;
    return RESULT_SUCCESS;
}

static ResultCode MapTransferMemory(Core::System& system, Handle handle, VAddr address, u64 size,
                                    u32 permission_raw) {
    LOG_DEBUG(Kernel_SVC,
              "called. handle=0x{:08X}, address=0x{:016X}, size=0x{:016X}, permissions=0x{:08X}",
              handle, address, size, permission_raw);

    if (!Common::Is4KBAligned(address)) {
        LOG_ERROR(Kernel_SVC, "Transfer memory addresses must be 4KB aligned (size=0x{:016X}).",
                  address);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0 || !Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC,
                  "Transfer memory sizes must be 4KB aligned and not be zero (size=0x{:016X}).",
                  size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Given address and size overflows the 64-bit range (address=0x{:016X}, "
                  "size=0x{:016X}).",
                  address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto permissions = static_cast<MemoryPermission>(permission_raw);
    if (permissions != MemoryPermission::None && permissions != MemoryPermission::Read &&
        permissions != MemoryPermission::ReadWrite) {
        LOG_ERROR(Kernel_SVC, "Invalid transfer memory permissions given (permissions=0x{:08X}).",
                  permission_raw);
        return ERR_INVALID_STATE;
    }

    const auto& kernel = system.Kernel();
    const auto* const current_process = kernel.CurrentProcess();
    const auto& handle_table = current_process->GetHandleTable();

    auto transfer_memory = handle_table.Get<TransferMemory>(handle);
    if (!transfer_memory) {
        LOG_ERROR(Kernel_SVC, "Nonexistent transfer memory handle given (handle=0x{:08X}).",
                  handle);
        return ERR_INVALID_HANDLE;
    }

    if (!current_process->VMManager().IsWithinASLRRegion(address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Given address and size don't fully fit within the ASLR region "
                  "(address=0x{:016X}, size=0x{:016X}).",
                  address, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return transfer_memory->MapMemory(address, size, permissions);
}

static ResultCode UnmapTransferMemory(Core::System& system, Handle handle, VAddr address,
                                      u64 size) {
    LOG_DEBUG(Kernel_SVC, "called. handle=0x{:08X}, address=0x{:016X}, size=0x{:016X}", handle,
              address, size);

    if (!Common::Is4KBAligned(address)) {
        LOG_ERROR(Kernel_SVC, "Transfer memory addresses must be 4KB aligned (size=0x{:016X}).",
                  address);
        return ERR_INVALID_ADDRESS;
    }

    if (size == 0 || !Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC,
                  "Transfer memory sizes must be 4KB aligned and not be zero (size=0x{:016X}).",
                  size);
        return ERR_INVALID_SIZE;
    }

    if (!IsValidAddressRange(address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Given address and size overflows the 64-bit range (address=0x{:016X}, "
                  "size=0x{:016X}).",
                  address, size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto& kernel = system.Kernel();
    const auto* const current_process = kernel.CurrentProcess();
    const auto& handle_table = current_process->GetHandleTable();

    auto transfer_memory = handle_table.Get<TransferMemory>(handle);
    if (!transfer_memory) {
        LOG_ERROR(Kernel_SVC, "Nonexistent transfer memory handle given (handle=0x{:08X}).",
                  handle);
        return ERR_INVALID_HANDLE;
    }

    if (!current_process->VMManager().IsWithinASLRRegion(address, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Given address and size don't fully fit within the ASLR region "
                  "(address=0x{:016X}, size=0x{:016X}).",
                  address, size);
        return ERR_INVALID_MEMORY_RANGE;
    }

    return transfer_memory->UnmapMemory(address, size);
}

static ResultCode GetThreadCoreMask(Core::System& system, Handle thread_handle, u32* core,
                                    u64* mask) {
    LOG_TRACE(Kernel_SVC, "called, handle=0x{:08X}", thread_handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, thread_handle=0x{:08X}",
                  thread_handle);
        return ERR_INVALID_HANDLE;
    }

    *core = thread->GetIdealCore();
    *mask = thread->GetAffinityMask();

    return RESULT_SUCCESS;
}

static ResultCode SetThreadCoreMask(Core::System& system, Handle thread_handle, u32 core,
                                    u64 affinity_mask) {
    LOG_DEBUG(Kernel_SVC, "called, handle=0x{:08X}, core=0x{:X}, affinity_mask=0x{:016X}",
              thread_handle, core, affinity_mask);

    const auto* const current_process = system.Kernel().CurrentProcess();

    if (core == static_cast<u32>(THREADPROCESSORID_IDEAL)) {
        const u8 ideal_cpu_core = current_process->GetIdealCore();

        ASSERT(ideal_cpu_core != static_cast<u8>(THREADPROCESSORID_IDEAL));

        // Set the target CPU to the ideal core specified by the process.
        core = ideal_cpu_core;
        affinity_mask = 1ULL << core;
    } else {
        const u64 core_mask = current_process->GetCoreMask();

        if ((core_mask | affinity_mask) != core_mask) {
            LOG_ERROR(
                Kernel_SVC,
                "Invalid processor ID specified (core_mask=0x{:08X}, affinity_mask=0x{:016X})",
                core_mask, affinity_mask);
            return ERR_INVALID_PROCESSOR_ID;
        }

        if (affinity_mask == 0) {
            LOG_ERROR(Kernel_SVC, "Specfified affinity mask is zero.");
            return ERR_INVALID_COMBINATION;
        }

        if (core < Core::NUM_CPU_CORES) {
            if ((affinity_mask & (1ULL << core)) == 0) {
                LOG_ERROR(Kernel_SVC,
                          "Core is not enabled for the current mask, core={}, mask={:016X}", core,
                          affinity_mask);
                return ERR_INVALID_COMBINATION;
            }
        } else if (core != static_cast<u32>(THREADPROCESSORID_DONT_CARE) &&
                   core != static_cast<u32>(THREADPROCESSORID_DONT_UPDATE)) {
            LOG_ERROR(Kernel_SVC, "Invalid processor ID specified (core={}).", core);
            return ERR_INVALID_PROCESSOR_ID;
        }
    }

    const auto& handle_table = current_process->GetHandleTable();
    const SharedPtr<Thread> thread = handle_table.Get<Thread>(thread_handle);
    if (!thread) {
        LOG_ERROR(Kernel_SVC, "Thread handle does not exist, thread_handle=0x{:08X}",
                  thread_handle);
        return ERR_INVALID_HANDLE;
    }

    system.PrepareReschedule(thread->GetProcessorID());
    thread->ChangeCore(core, affinity_mask);
    system.PrepareReschedule(thread->GetProcessorID());

    return RESULT_SUCCESS;
}

static ResultCode CreateSharedMemory(Core::System& system, Handle* handle, u64 size,
                                     u32 local_permissions, u32 remote_permissions) {
    LOG_TRACE(Kernel_SVC, "called, size=0x{:X}, localPerms=0x{:08X}, remotePerms=0x{:08X}", size,
              local_permissions, remote_permissions);
    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        return ERR_INVALID_SIZE;
    }
    if (!Common::Is4KBAligned(size)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, 0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    if (size >= MAIN_MEMORY_SIZE) {
        LOG_ERROR(Kernel_SVC, "Size is not less than 8GB, 0x{:016X}", size);
        return ERR_INVALID_SIZE;
    }

    const auto local_perms = static_cast<MemoryPermission>(local_permissions);
    if (local_perms != MemoryPermission::Read && local_perms != MemoryPermission::ReadWrite) {
        LOG_ERROR(Kernel_SVC,
                  "Invalid local memory permissions, expected Read or ReadWrite but got "
                  "local_permissions={}",
                  static_cast<u32>(local_permissions));
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    const auto remote_perms = static_cast<MemoryPermission>(remote_permissions);
    if (remote_perms != MemoryPermission::Read && remote_perms != MemoryPermission::ReadWrite &&
        remote_perms != MemoryPermission::DontCare) {
        LOG_ERROR(Kernel_SVC,
                  "Invalid remote memory permissions, expected Read, ReadWrite or DontCare but got "
                  "remote_permissions={}",
                  static_cast<u32>(remote_permissions));
        return ERR_INVALID_MEMORY_PERMISSIONS;
    }

    auto& kernel = system.Kernel();
    auto process = kernel.CurrentProcess();
    auto& handle_table = process->GetHandleTable();
    auto shared_mem_handle = SharedMemory::Create(kernel, process, size, local_perms, remote_perms);

    CASCADE_RESULT(*handle, handle_table.Create(shared_mem_handle));
    return RESULT_SUCCESS;
}

static ResultCode CreateEvent(Core::System& system, Handle* write_handle, Handle* read_handle) {
    LOG_DEBUG(Kernel_SVC, "called");

    auto& kernel = system.Kernel();
    const auto [readable_event, writable_event] =
        WritableEvent::CreateEventPair(kernel, ResetType::Manual, "CreateEvent");

    HandleTable& handle_table = kernel.CurrentProcess()->GetHandleTable();

    const auto write_create_result = handle_table.Create(writable_event);
    if (write_create_result.Failed()) {
        return write_create_result.Code();
    }
    *write_handle = *write_create_result;

    const auto read_create_result = handle_table.Create(readable_event);
    if (read_create_result.Failed()) {
        handle_table.Close(*write_create_result);
        return read_create_result.Code();
    }
    *read_handle = *read_create_result;

    LOG_DEBUG(Kernel_SVC,
              "successful. Writable event handle=0x{:08X}, Readable event handle=0x{:08X}",
              *write_create_result, *read_create_result);
    return RESULT_SUCCESS;
}

static ResultCode ClearEvent(Core::System& system, Handle handle) {
    LOG_TRACE(Kernel_SVC, "called, event=0x{:08X}", handle);

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();

    auto writable_event = handle_table.Get<WritableEvent>(handle);
    if (writable_event) {
        writable_event->Clear();
        return RESULT_SUCCESS;
    }

    auto readable_event = handle_table.Get<ReadableEvent>(handle);
    if (readable_event) {
        readable_event->Clear();
        return RESULT_SUCCESS;
    }

    LOG_ERROR(Kernel_SVC, "Event handle does not exist, handle=0x{:08X}", handle);
    return ERR_INVALID_HANDLE;
}

static ResultCode SignalEvent(Core::System& system, Handle handle) {
    LOG_DEBUG(Kernel_SVC, "called. Handle=0x{:08X}", handle);

    HandleTable& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    auto writable_event = handle_table.Get<WritableEvent>(handle);

    if (!writable_event) {
        LOG_ERROR(Kernel_SVC, "Non-existent writable event handle used (0x{:08X})", handle);
        return ERR_INVALID_HANDLE;
    }

    writable_event->Signal();
    system.PrepareReschedule();
    return RESULT_SUCCESS;
}

static ResultCode GetProcessInfo(Core::System& system, u64* out, Handle process_handle, u32 type) {
    LOG_DEBUG(Kernel_SVC, "called, handle=0x{:08X}, type=0x{:X}", process_handle, type);

    // This function currently only allows retrieving a process' status.
    enum class InfoType {
        Status,
    };

    const auto& handle_table = system.Kernel().CurrentProcess()->GetHandleTable();
    const auto process = handle_table.Get<Process>(process_handle);
    if (!process) {
        LOG_ERROR(Kernel_SVC, "Process handle does not exist, process_handle=0x{:08X}",
                  process_handle);
        return ERR_INVALID_HANDLE;
    }

    const auto info_type = static_cast<InfoType>(type);
    if (info_type != InfoType::Status) {
        LOG_ERROR(Kernel_SVC, "Expected info_type to be Status but got {} instead", type);
        return ERR_INVALID_ENUM_VALUE;
    }

    *out = static_cast<u64>(process->GetStatus());
    return RESULT_SUCCESS;
}

static ResultCode CreateResourceLimit(Core::System& system, Handle* out_handle) {
    LOG_DEBUG(Kernel_SVC, "called");

    auto& kernel = system.Kernel();
    auto resource_limit = ResourceLimit::Create(kernel);

    auto* const current_process = kernel.CurrentProcess();
    ASSERT(current_process != nullptr);

    const auto handle = current_process->GetHandleTable().Create(std::move(resource_limit));
    if (handle.Failed()) {
        return handle.Code();
    }

    *out_handle = *handle;
    return RESULT_SUCCESS;
}

static ResultCode GetResourceLimitLimitValue(Core::System& system, u64* out_value,
                                             Handle resource_limit, u32 resource_type) {
    LOG_DEBUG(Kernel_SVC, "called. Handle={:08X}, Resource type={}", resource_limit, resource_type);

    const auto limit_value = RetrieveResourceLimitValue(system, resource_limit, resource_type,
                                                        ResourceLimitValueType::LimitValue);
    if (limit_value.Failed()) {
        return limit_value.Code();
    }

    *out_value = static_cast<u64>(*limit_value);
    return RESULT_SUCCESS;
}

static ResultCode GetResourceLimitCurrentValue(Core::System& system, u64* out_value,
                                               Handle resource_limit, u32 resource_type) {
    LOG_DEBUG(Kernel_SVC, "called. Handle={:08X}, Resource type={}", resource_limit, resource_type);

    const auto current_value = RetrieveResourceLimitValue(system, resource_limit, resource_type,
                                                          ResourceLimitValueType::CurrentValue);
    if (current_value.Failed()) {
        return current_value.Code();
    }

    *out_value = static_cast<u64>(*current_value);
    return RESULT_SUCCESS;
}

static ResultCode SetResourceLimitLimitValue(Core::System& system, Handle resource_limit,
                                             u32 resource_type, u64 value) {
    LOG_DEBUG(Kernel_SVC, "called. Handle={:08X}, Resource type={}, Value={}", resource_limit,
              resource_type, value);

    const auto type = static_cast<ResourceType>(resource_type);
    if (!IsValidResourceType(type)) {
        LOG_ERROR(Kernel_SVC, "Invalid resource limit type: '{}'", resource_type);
        return ERR_INVALID_ENUM_VALUE;
    }

    auto* const current_process = system.Kernel().CurrentProcess();
    ASSERT(current_process != nullptr);

    auto resource_limit_object =
        current_process->GetHandleTable().Get<ResourceLimit>(resource_limit);
    if (!resource_limit_object) {
        LOG_ERROR(Kernel_SVC, "Handle to non-existent resource limit instance used. Handle={:08X}",
                  resource_limit);
        return ERR_INVALID_HANDLE;
    }

    const auto set_result = resource_limit_object->SetLimitValue(type, static_cast<s64>(value));
    if (set_result.IsError()) {
        LOG_ERROR(
            Kernel_SVC,
            "Attempted to lower resource limit ({}) for category '{}' below its current value ({})",
            resource_limit_object->GetMaxResourceValue(type), resource_type,
            resource_limit_object->GetCurrentResourceValue(type));
        return set_result;
    }

    return RESULT_SUCCESS;
}

static ResultCode GetProcessList(Core::System& system, u32* out_num_processes,
                                 VAddr out_process_ids, u32 out_process_ids_size) {
    LOG_DEBUG(Kernel_SVC, "called. out_process_ids=0x{:016X}, out_process_ids_size={}",
              out_process_ids, out_process_ids_size);

    // If the supplied size is negative or greater than INT32_MAX / sizeof(u64), bail.
    if ((out_process_ids_size & 0xF0000000) != 0) {
        LOG_ERROR(Kernel_SVC,
                  "Supplied size outside [0, 0x0FFFFFFF] range. out_process_ids_size={}",
                  out_process_ids_size);
        return ERR_OUT_OF_RANGE;
    }

    const auto& kernel = system.Kernel();
    const auto& vm_manager = kernel.CurrentProcess()->VMManager();
    const auto total_copy_size = out_process_ids_size * sizeof(u64);

    if (out_process_ids_size > 0 &&
        !vm_manager.IsWithinAddressSpace(out_process_ids, total_copy_size)) {
        LOG_ERROR(Kernel_SVC, "Address range outside address space. begin=0x{:016X}, end=0x{:016X}",
                  out_process_ids, out_process_ids + total_copy_size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto& process_list = kernel.GetProcessList();
    const auto num_processes = process_list.size();
    const auto copy_amount = std::min(std::size_t{out_process_ids_size}, num_processes);

    for (std::size_t i = 0; i < copy_amount; ++i) {
        Memory::Write64(out_process_ids, process_list[i]->GetProcessID());
        out_process_ids += sizeof(u64);
    }

    *out_num_processes = static_cast<u32>(num_processes);
    return RESULT_SUCCESS;
}

static ResultCode GetThreadList(Core::System& system, u32* out_num_threads, VAddr out_thread_ids,
                                u32 out_thread_ids_size, Handle debug_handle) {
    // TODO: Handle this case when debug events are supported.
    UNIMPLEMENTED_IF(debug_handle != InvalidHandle);

    LOG_DEBUG(Kernel_SVC, "called. out_thread_ids=0x{:016X}, out_thread_ids_size={}",
              out_thread_ids, out_thread_ids_size);

    // If the size is negative or larger than INT32_MAX / sizeof(u64)
    if ((out_thread_ids_size & 0xF0000000) != 0) {
        LOG_ERROR(Kernel_SVC, "Supplied size outside [0, 0x0FFFFFFF] range. size={}",
                  out_thread_ids_size);
        return ERR_OUT_OF_RANGE;
    }

    const auto* const current_process = system.Kernel().CurrentProcess();
    const auto& vm_manager = current_process->VMManager();
    const auto total_copy_size = out_thread_ids_size * sizeof(u64);

    if (out_thread_ids_size > 0 &&
        !vm_manager.IsWithinAddressSpace(out_thread_ids, total_copy_size)) {
        LOG_ERROR(Kernel_SVC, "Address range outside address space. begin=0x{:016X}, end=0x{:016X}",
                  out_thread_ids, out_thread_ids + total_copy_size);
        return ERR_INVALID_ADDRESS_STATE;
    }

    const auto& thread_list = current_process->GetThreadList();
    const auto num_threads = thread_list.size();
    const auto copy_amount = std::min(std::size_t{out_thread_ids_size}, num_threads);

    auto list_iter = thread_list.cbegin();
    for (std::size_t i = 0; i < copy_amount; ++i, ++list_iter) {
        Memory::Write64(out_thread_ids, (*list_iter)->GetThreadID());
        out_thread_ids += sizeof(u64);
    }

    *out_num_threads = static_cast<u32>(num_threads);
    return RESULT_SUCCESS;
}

namespace {
struct FunctionDef {
    using Func = void(Core::System&);

    u32 id;
    Func* func;
    const char* name;
};
} // namespace

static const FunctionDef SVC_Table[] = {
    {0x00, nullptr, "Unknown"},
    {0x01, SvcWrap<SetHeapSize>, "SetHeapSize"},
    {0x02, SvcWrap<SetMemoryPermission>, "SetMemoryPermission"},
    {0x03, SvcWrap<SetMemoryAttribute>, "SetMemoryAttribute"},
    {0x04, SvcWrap<MapMemory>, "MapMemory"},
    {0x05, SvcWrap<UnmapMemory>, "UnmapMemory"},
    {0x06, SvcWrap<QueryMemory>, "QueryMemory"},
    {0x07, SvcWrap<ExitProcess>, "ExitProcess"},
    {0x08, SvcWrap<CreateThread>, "CreateThread"},
    {0x09, SvcWrap<StartThread>, "StartThread"},
    {0x0A, SvcWrap<ExitThread>, "ExitThread"},
    {0x0B, SvcWrap<SleepThread>, "SleepThread"},
    {0x0C, SvcWrap<GetThreadPriority>, "GetThreadPriority"},
    {0x0D, SvcWrap<SetThreadPriority>, "SetThreadPriority"},
    {0x0E, SvcWrap<GetThreadCoreMask>, "GetThreadCoreMask"},
    {0x0F, SvcWrap<SetThreadCoreMask>, "SetThreadCoreMask"},
    {0x10, SvcWrap<GetCurrentProcessorNumber>, "GetCurrentProcessorNumber"},
    {0x11, SvcWrap<SignalEvent>, "SignalEvent"},
    {0x12, SvcWrap<ClearEvent>, "ClearEvent"},
    {0x13, SvcWrap<MapSharedMemory>, "MapSharedMemory"},
    {0x14, SvcWrap<UnmapSharedMemory>, "UnmapSharedMemory"},
    {0x15, SvcWrap<CreateTransferMemory>, "CreateTransferMemory"},
    {0x16, SvcWrap<CloseHandle>, "CloseHandle"},
    {0x17, SvcWrap<ResetSignal>, "ResetSignal"},
    {0x18, SvcWrap<WaitSynchronization>, "WaitSynchronization"},
    {0x19, SvcWrap<CancelSynchronization>, "CancelSynchronization"},
    {0x1A, SvcWrap<ArbitrateLock>, "ArbitrateLock"},
    {0x1B, SvcWrap<ArbitrateUnlock>, "ArbitrateUnlock"},
    {0x1C, SvcWrap<WaitProcessWideKeyAtomic>, "WaitProcessWideKeyAtomic"},
    {0x1D, SvcWrap<SignalProcessWideKey>, "SignalProcessWideKey"},
    {0x1E, SvcWrap<GetSystemTick>, "GetSystemTick"},
    {0x1F, SvcWrap<ConnectToNamedPort>, "ConnectToNamedPort"},
    {0x20, nullptr, "SendSyncRequestLight"},
    {0x21, SvcWrap<SendSyncRequest>, "SendSyncRequest"},
    {0x22, nullptr, "SendSyncRequestWithUserBuffer"},
    {0x23, nullptr, "SendAsyncRequestWithUserBuffer"},
    {0x24, SvcWrap<GetProcessId>, "GetProcessId"},
    {0x25, SvcWrap<GetThreadId>, "GetThreadId"},
    {0x26, SvcWrap<Break>, "Break"},
    {0x27, SvcWrap<OutputDebugString>, "OutputDebugString"},
    {0x28, nullptr, "ReturnFromException"},
    {0x29, SvcWrap<GetInfo>, "GetInfo"},
    {0x2A, nullptr, "FlushEntireDataCache"},
    {0x2B, nullptr, "FlushDataCache"},
    {0x2C, SvcWrap<MapPhysicalMemory>, "MapPhysicalMemory"},
    {0x2D, SvcWrap<UnmapPhysicalMemory>, "UnmapPhysicalMemory"},
    {0x2E, nullptr, "GetFutureThreadInfo"},
    {0x2F, nullptr, "GetLastThreadInfo"},
    {0x30, SvcWrap<GetResourceLimitLimitValue>, "GetResourceLimitLimitValue"},
    {0x31, SvcWrap<GetResourceLimitCurrentValue>, "GetResourceLimitCurrentValue"},
    {0x32, SvcWrap<SetThreadActivity>, "SetThreadActivity"},
    {0x33, SvcWrap<GetThreadContext>, "GetThreadContext"},
    {0x34, SvcWrap<WaitForAddress>, "WaitForAddress"},
    {0x35, SvcWrap<SignalToAddress>, "SignalToAddress"},
    {0x36, nullptr, "SynchronizePreemptionState"},
    {0x37, nullptr, "Unknown"},
    {0x38, nullptr, "Unknown"},
    {0x39, nullptr, "Unknown"},
    {0x3A, nullptr, "Unknown"},
    {0x3B, nullptr, "Unknown"},
    {0x3C, nullptr, "DumpInfo"},
    {0x3D, nullptr, "DumpInfoNew"},
    {0x3E, nullptr, "Unknown"},
    {0x3F, nullptr, "Unknown"},
    {0x40, nullptr, "CreateSession"},
    {0x41, nullptr, "AcceptSession"},
    {0x42, nullptr, "ReplyAndReceiveLight"},
    {0x43, nullptr, "ReplyAndReceive"},
    {0x44, nullptr, "ReplyAndReceiveWithUserBuffer"},
    {0x45, SvcWrap<CreateEvent>, "CreateEvent"},
    {0x46, nullptr, "Unknown"},
    {0x47, nullptr, "Unknown"},
    {0x48, nullptr, "MapPhysicalMemoryUnsafe"},
    {0x49, nullptr, "UnmapPhysicalMemoryUnsafe"},
    {0x4A, nullptr, "SetUnsafeLimit"},
    {0x4B, nullptr, "CreateCodeMemory"},
    {0x4C, nullptr, "ControlCodeMemory"},
    {0x4D, nullptr, "SleepSystem"},
    {0x4E, nullptr, "ReadWriteRegister"},
    {0x4F, nullptr, "SetProcessActivity"},
    {0x50, SvcWrap<CreateSharedMemory>, "CreateSharedMemory"},
    {0x51, SvcWrap<MapTransferMemory>, "MapTransferMemory"},
    {0x52, SvcWrap<UnmapTransferMemory>, "UnmapTransferMemory"},
    {0x53, nullptr, "CreateInterruptEvent"},
    {0x54, nullptr, "QueryPhysicalAddress"},
    {0x55, nullptr, "QueryIoMapping"},
    {0x56, nullptr, "CreateDeviceAddressSpace"},
    {0x57, nullptr, "AttachDeviceAddressSpace"},
    {0x58, nullptr, "DetachDeviceAddressSpace"},
    {0x59, nullptr, "MapDeviceAddressSpaceByForce"},
    {0x5A, nullptr, "MapDeviceAddressSpaceAligned"},
    {0x5B, nullptr, "MapDeviceAddressSpace"},
    {0x5C, nullptr, "UnmapDeviceAddressSpace"},
    {0x5D, nullptr, "InvalidateProcessDataCache"},
    {0x5E, nullptr, "StoreProcessDataCache"},
    {0x5F, nullptr, "FlushProcessDataCache"},
    {0x60, nullptr, "DebugActiveProcess"},
    {0x61, nullptr, "BreakDebugProcess"},
    {0x62, nullptr, "TerminateDebugProcess"},
    {0x63, nullptr, "GetDebugEvent"},
    {0x64, nullptr, "ContinueDebugEvent"},
    {0x65, SvcWrap<GetProcessList>, "GetProcessList"},
    {0x66, SvcWrap<GetThreadList>, "GetThreadList"},
    {0x67, nullptr, "GetDebugThreadContext"},
    {0x68, nullptr, "SetDebugThreadContext"},
    {0x69, nullptr, "QueryDebugProcessMemory"},
    {0x6A, nullptr, "ReadDebugProcessMemory"},
    {0x6B, nullptr, "WriteDebugProcessMemory"},
    {0x6C, nullptr, "SetHardwareBreakPoint"},
    {0x6D, nullptr, "GetDebugThreadParam"},
    {0x6E, nullptr, "Unknown"},
    {0x6F, nullptr, "GetSystemInfo"},
    {0x70, nullptr, "CreatePort"},
    {0x71, nullptr, "ManageNamedPort"},
    {0x72, nullptr, "ConnectToPort"},
    {0x73, nullptr, "SetProcessMemoryPermission"},
    {0x74, nullptr, "MapProcessMemory"},
    {0x75, nullptr, "UnmapProcessMemory"},
    {0x76, SvcWrap<QueryProcessMemory>, "QueryProcessMemory"},
    {0x77, SvcWrap<MapProcessCodeMemory>, "MapProcessCodeMemory"},
    {0x78, SvcWrap<UnmapProcessCodeMemory>, "UnmapProcessCodeMemory"},
    {0x79, nullptr, "CreateProcess"},
    {0x7A, nullptr, "StartProcess"},
    {0x7B, nullptr, "TerminateProcess"},
    {0x7C, SvcWrap<GetProcessInfo>, "GetProcessInfo"},
    {0x7D, SvcWrap<CreateResourceLimit>, "CreateResourceLimit"},
    {0x7E, SvcWrap<SetResourceLimitLimitValue>, "SetResourceLimitLimitValue"},
    {0x7F, nullptr, "CallSecureMonitor"},
};

static const FunctionDef* GetSVCInfo(u32 func_num) {
    if (func_num >= std::size(SVC_Table)) {
        LOG_ERROR(Kernel_SVC, "Unknown svc=0x{:02X}", func_num);
        return nullptr;
    }
    return &SVC_Table[func_num];
}

MICROPROFILE_DEFINE(Kernel_SVC, "Kernel", "SVC", MP_RGB(70, 200, 70));

void CallSVC(Core::System& system, u32 immediate) {
    MICROPROFILE_SCOPE(Kernel_SVC);

    // Lock the global kernel mutex when we enter the kernel HLE.
    std::lock_guard lock{HLE::g_hle_lock};

    const FunctionDef* info = GetSVCInfo(immediate);
    if (info) {
        if (info->func) {
            info->func(system);
        } else {
            LOG_CRITICAL(Kernel_SVC, "Unimplemented SVC function {}(..)", info->name);
        }
    } else {
        LOG_CRITICAL(Kernel_SVC, "Unknown SVC function 0x{:X}", immediate);
    }
}

} // namespace Kernel
