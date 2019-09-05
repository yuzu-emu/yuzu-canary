// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/hle/kernel/address_arbiter.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/memory.h"

namespace Kernel {
namespace {
// Wake up num_to_wake (or all) threads in a vector.
void WakeThreads(const std::vector<SharedPtr<Thread>>& waiting_threads, s32 num_to_wake) {
    auto& system = Core::System::GetInstance();
    // Only process up to 'target' threads, unless 'target' is <= 0, in which case process
    // them all.
    std::size_t last = waiting_threads.size();
    if (num_to_wake > 0) {
        last = std::min(last, static_cast<std::size_t>(num_to_wake));
    }

    // Signal the waiting threads.
    for (std::size_t i = 0; i < last; i++) {
        ASSERT(waiting_threads[i]->GetStatus() == ThreadStatus::WaitArb);
        waiting_threads[i]->SetWaitSynchronizationResult(RESULT_SUCCESS);
        waiting_threads[i]->SetArbiterWaitAddress(0);
        waiting_threads[i]->ResumeFromWait();
        system.PrepareReschedule(waiting_threads[i]->GetProcessorID());
    }
}
} // Anonymous namespace

AddressArbiter::AddressArbiter(Core::System& system) : system{system} {}
AddressArbiter::~AddressArbiter() = default;

ResultCode AddressArbiter::SignalToAddress(VAddr address, SignalType type, s32 value,
                                           s32 num_to_wake) {
    switch (type) {
    case SignalType::Signal:
        return SignalToAddressOnly(address, num_to_wake);
    case SignalType::IncrementAndSignalIfEqual:
        return IncrementAndSignalToAddressIfEqual(address, value, num_to_wake);
    case SignalType::ModifyByWaitingCountAndSignalIfEqual:
        return ModifyByWaitingCountAndSignalToAddressIfEqual(address, value, num_to_wake);
    default:
        return ERR_INVALID_ENUM_VALUE;
    }
}

ResultCode AddressArbiter::SignalToAddressOnly(VAddr address, s32 num_to_wake) {
    const std::vector<SharedPtr<Thread>> waiting_threads = GetThreadsWaitingOnAddress(address);
    WakeThreads(waiting_threads, num_to_wake);
    return RESULT_SUCCESS;
}

ResultCode AddressArbiter::IncrementAndSignalToAddressIfEqual(VAddr address, s32 value,
                                                              s32 num_to_wake) {
    // Ensure that we can write to the address.
    if (!Memory::IsValidVirtualAddress(address)) {
        return ERR_INVALID_ADDRESS_STATE;
    }

    if (static_cast<s32>(Memory::Read32(address)) != value) {
        return ERR_INVALID_STATE;
    }

    Memory::Write32(address, static_cast<u32>(value + 1));
    return SignalToAddressOnly(address, num_to_wake);
}

ResultCode AddressArbiter::ModifyByWaitingCountAndSignalToAddressIfEqual(VAddr address, s32 value,
                                                                         s32 num_to_wake) {
    // Ensure that we can write to the address.
    if (!Memory::IsValidVirtualAddress(address)) {
        return ERR_INVALID_ADDRESS_STATE;
    }

    // Get threads waiting on the address.
    const std::vector<SharedPtr<Thread>> waiting_threads = GetThreadsWaitingOnAddress(address);

    // Determine the modified value depending on the waiting count.
    s32 updated_value;
    if (waiting_threads.empty()) {
        updated_value = value + 1;
    } else if (num_to_wake <= 0 || waiting_threads.size() <= static_cast<u32>(num_to_wake)) {
        updated_value = value - 1;
    } else {
        updated_value = value;
    }

    if (static_cast<s32>(Memory::Read32(address)) != value) {
        return ERR_INVALID_STATE;
    }

    Memory::Write32(address, static_cast<u32>(updated_value));
    WakeThreads(waiting_threads, num_to_wake);
    return RESULT_SUCCESS;
}

ResultCode AddressArbiter::WaitForAddress(VAddr address, ArbitrationType type, s32 value,
                                          s64 timeout_ns) {
    switch (type) {
    case ArbitrationType::WaitIfLessThan:
        return WaitForAddressIfLessThan(address, value, timeout_ns, false);
    case ArbitrationType::DecrementAndWaitIfLessThan:
        return WaitForAddressIfLessThan(address, value, timeout_ns, true);
    case ArbitrationType::WaitIfEqual:
        return WaitForAddressIfEqual(address, value, timeout_ns);
    default:
        return ERR_INVALID_ENUM_VALUE;
    }
}

ResultCode AddressArbiter::WaitForAddressIfLessThan(VAddr address, s32 value, s64 timeout,
                                                    bool should_decrement) {
    // Ensure that we can read the address.
    if (!Memory::IsValidVirtualAddress(address)) {
        return ERR_INVALID_ADDRESS_STATE;
    }

    const s32 cur_value = static_cast<s32>(Memory::Read32(address));
    if (cur_value >= value) {
        return ERR_INVALID_STATE;
    }

    if (should_decrement) {
        Memory::Write32(address, static_cast<u32>(cur_value - 1));
    }

    // Short-circuit without rescheduling, if timeout is zero.
    if (timeout == 0) {
        return RESULT_TIMEOUT;
    }

    return WaitForAddressImpl(address, timeout);
}

ResultCode AddressArbiter::WaitForAddressIfEqual(VAddr address, s32 value, s64 timeout) {
    // Ensure that we can read the address.
    if (!Memory::IsValidVirtualAddress(address)) {
        return ERR_INVALID_ADDRESS_STATE;
    }
    // Only wait for the address if equal.
    if (static_cast<s32>(Memory::Read32(address)) != value) {
        return ERR_INVALID_STATE;
    }
    // Short-circuit without rescheduling, if timeout is zero.
    if (timeout == 0) {
        return RESULT_TIMEOUT;
    }

    return WaitForAddressImpl(address, timeout);
}

ResultCode AddressArbiter::WaitForAddressImpl(VAddr address, s64 timeout) {
    SharedPtr<Thread> current_thread = system.CurrentScheduler().GetCurrentThread();
    current_thread->SetArbiterWaitAddress(address);
    current_thread->SetStatus(ThreadStatus::WaitArb);
    current_thread->InvalidateWakeupCallback();

    current_thread->WakeAfterDelay(timeout);

    system.PrepareReschedule(current_thread->GetProcessorID());
    return RESULT_TIMEOUT;
}

std::vector<SharedPtr<Thread>> AddressArbiter::GetThreadsWaitingOnAddress(VAddr address) const {

    // Retrieve all threads that are waiting for this address.
    std::vector<SharedPtr<Thread>> threads;
    const auto& scheduler = system.GlobalScheduler();
    const auto& thread_list = scheduler.GetThreadList();

    for (const auto& thread : thread_list) {
        if (thread->GetArbiterWaitAddress() == address) {
            threads.push_back(thread);
        }
    }

    // Sort them by priority, such that the highest priority ones come first.
    std::sort(threads.begin(), threads.end(),
              [](const SharedPtr<Thread>& lhs, const SharedPtr<Thread>& rhs) {
                  return lhs->GetPriority() < rhs->GetPriority();
              });

    return threads;
}
} // namespace Kernel
