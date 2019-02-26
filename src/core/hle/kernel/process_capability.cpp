// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/bit_util.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/process_capability.h"
#include "core/hle/kernel/vm_manager.h"

namespace Kernel {
namespace {

// clang-format off

// Shift offsets for kernel capability types.
enum : u32 {
    CapabilityOffset_PriorityAndCoreNum = 3,
    CapabilityOffset_Syscall            = 4,
    CapabilityOffset_MapPhysical        = 6,
    CapabilityOffset_MapIO              = 7,
    CapabilityOffset_Interrupt          = 11,
    CapabilityOffset_ProgramType        = 13,
    CapabilityOffset_KernelVersion      = 14,
    CapabilityOffset_HandleTableSize    = 15,
    CapabilityOffset_Debug              = 16,
};

// Combined mask of all parameters that may be initialized only once.
constexpr u32 InitializeOnceMask = (1U << CapabilityOffset_PriorityAndCoreNum) |
                                   (1U << CapabilityOffset_ProgramType) |
                                   (1U << CapabilityOffset_KernelVersion) |
                                   (1U << CapabilityOffset_HandleTableSize) |
                                   (1U << CapabilityOffset_Debug);

// Packed kernel version indicating 10.4.0
constexpr u32 PackedKernelVersion = 0x520000;

// Indicates possible types of capabilities that can be specified.
enum class CapabilityType : u32 {
    Unset              = 0U,
    PriorityAndCoreNum = (1U << CapabilityOffset_PriorityAndCoreNum) - 1,
    Syscall            = (1U << CapabilityOffset_Syscall) - 1,
    MapPhysical        = (1U << CapabilityOffset_MapPhysical) - 1,
    MapIO              = (1U << CapabilityOffset_MapIO) - 1,
    Interrupt          = (1U << CapabilityOffset_Interrupt) - 1,
    ProgramType        = (1U << CapabilityOffset_ProgramType) - 1,
    KernelVersion      = (1U << CapabilityOffset_KernelVersion) - 1,
    HandleTableSize    = (1U << CapabilityOffset_HandleTableSize) - 1,
    Debug              = (1U << CapabilityOffset_Debug) - 1,
    Ignorable          = 0xFFFFFFFFU,
};

// clang-format on

constexpr CapabilityType GetCapabilityType(u32 value) {
    return static_cast<CapabilityType>((~value & (value + 1)) - 1);
}

u32 GetFlagBitOffset(CapabilityType type) {
    const auto value = static_cast<u32>(type);
    return static_cast<u32>(Common::BitSize<u32>() - Common::CountLeadingZeroes32(value));
}

} // Anonymous namespace

ResultCode ProcessCapabilities::InitializeForKernelProcess(const u32* capabilities,
                                                           std::size_t num_capabilities,
                                                           VMManager& vm_manager) {
    Clear();

    // Allow all cores and priorities.
    core_mask = 0xF;
    priority_mask = 0xFFFFFFFFFFFFFFFF;
    kernel_version = PackedKernelVersion;

    return ParseCapabilities(capabilities, num_capabilities, vm_manager);
}

ResultCode ProcessCapabilities::InitializeForUserProcess(const u32* capabilities,
                                                         std::size_t num_capabilities,
                                                         VMManager& vm_manager) {
    Clear();

    return ParseCapabilities(capabilities, num_capabilities, vm_manager);
}

void ProcessCapabilities::InitializeForMetadatalessProcess() {
    // Allow all cores and priorities
    core_mask = 0xF;
    priority_mask = 0xFFFFFFFFFFFFFFFF;
    kernel_version = PackedKernelVersion;

    // Allow all system calls and interrupts.
    svc_capabilities.set();
    interrupt_capabilities.set();

    // Allow using the maximum possible amount of handles
    handle_table_size = static_cast<s32>(HandleTable::MAX_COUNT);

    // Allow all debugging capabilities.
    is_debuggable = true;
    can_force_debug = true;
}

ResultCode ProcessCapabilities::ParseCapabilities(const u32* capabilities,
                                                  std::size_t num_capabilities,
                                                  VMManager& vm_manager) {
    u32 set_flags = 0;
    u32 set_svc_bits = 0;

    for (std::size_t i = 0; i < num_capabilities; ++i) {
        const u32 descriptor = capabilities[i];
        const auto type = GetCapabilityType(descriptor);

        if (type == CapabilityType::MapPhysical) {
            i++;

            // The MapPhysical type uses two descriptor flags for its parameters.
            // If there's only one, then there's a problem.
            if (i >= num_capabilities) {
                return ERR_INVALID_COMBINATION;
            }

            const auto size_flags = capabilities[i];
            if (GetCapabilityType(size_flags) != CapabilityType::MapPhysical) {
                return ERR_INVALID_COMBINATION;
            }

            const auto result = HandleMapPhysicalFlags(descriptor, size_flags, vm_manager);
            if (result.IsError()) {
                return result;
            }
        } else {
            const auto result =
                ParseSingleFlagCapability(set_flags, set_svc_bits, descriptor, vm_manager);
            if (result.IsError()) {
                return result;
            }
        }
    }

    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::ParseSingleFlagCapability(u32& set_flags, u32& set_svc_bits,
                                                          u32 flag, VMManager& vm_manager) {
    const auto type = GetCapabilityType(flag);

    if (type == CapabilityType::Unset) {
        return ERR_INVALID_CAPABILITY_DESCRIPTOR;
    }

    // Bail early on ignorable entries, as one would expect,
    // ignorable descriptors can be ignored.
    if (type == CapabilityType::Ignorable) {
        return RESULT_SUCCESS;
    }

    // Ensure that the give flag hasn't already been initialized before.
    // If it has been, then bail.
    const u32 flag_length = GetFlagBitOffset(type);
    const u32 set_flag = 1U << flag_length;
    if ((set_flag & set_flags & InitializeOnceMask) != 0) {
        return ERR_INVALID_COMBINATION;
    }
    set_flags |= set_flag;

    switch (type) {
    case CapabilityType::PriorityAndCoreNum:
        return HandlePriorityCoreNumFlags(flag);
    case CapabilityType::Syscall:
        return HandleSyscallFlags(set_svc_bits, flag);
    case CapabilityType::MapIO:
        return HandleMapIOFlags(flag, vm_manager);
    case CapabilityType::Interrupt:
        return HandleInterruptFlags(flag);
    case CapabilityType::ProgramType:
        return HandleProgramTypeFlags(flag);
    case CapabilityType::KernelVersion:
        return HandleKernelVersionFlags(flag);
    case CapabilityType::HandleTableSize:
        return HandleHandleTableFlags(flag);
    case CapabilityType::Debug:
        return HandleDebugFlags(flag);
    default:
        break;
    }

    return ERR_INVALID_CAPABILITY_DESCRIPTOR;
}

void ProcessCapabilities::Clear() {
    svc_capabilities.reset();
    interrupt_capabilities.reset();

    core_mask = 0;
    priority_mask = 0;

    handle_table_size = 0;
    kernel_version = 0;

    program_type = ProgramType::SysModule;

    is_debuggable = false;
    can_force_debug = false;
}

ResultCode ProcessCapabilities::HandlePriorityCoreNumFlags(u32 flags) {
    if (priority_mask != 0 || core_mask != 0) {
        return ERR_INVALID_CAPABILITY_DESCRIPTOR;
    }

    const u32 core_num_min = (flags >> 16) & 0xFF;
    const u32 core_num_max = (flags >> 24) & 0xFF;
    if (core_num_min > core_num_max) {
        return ERR_INVALID_COMBINATION;
    }

    const u32 priority_min = (flags >> 10) & 0x3F;
    const u32 priority_max = (flags >> 4) & 0x3F;
    if (priority_min > priority_max) {
        return ERR_INVALID_COMBINATION;
    }

    // The switch only has 4 usable cores.
    if (core_num_max >= 4) {
        return ERR_INVALID_PROCESSOR_ID;
    }

    const auto make_mask = [](u64 min, u64 max) {
        const u64 range = max - min + 1;
        const u64 mask = (1ULL << range) - 1;

        return mask << min;
    };

    core_mask = make_mask(core_num_min, core_num_max);
    priority_mask = make_mask(priority_min, priority_max);
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleSyscallFlags(u32& set_svc_bits, u32 flags) {
    const u32 index = flags >> 29;
    const u32 svc_bit = 1U << index;

    // If we've already set this svc before, bail.
    if ((set_svc_bits & svc_bit) != 0) {
        return ERR_INVALID_COMBINATION;
    }
    set_svc_bits |= svc_bit;

    const u32 svc_mask = (flags >> 5) & 0xFFFFFF;
    for (u32 i = 0; i < 24; ++i) {
        const u32 svc_number = index * 24 + i;

        if ((svc_mask & (1U << i)) == 0) {
            continue;
        }

        if (svc_number >= svc_capabilities.size()) {
            return ERR_OUT_OF_RANGE;
        }

        svc_capabilities[svc_number] = true;
    }

    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleMapPhysicalFlags(u32 flags, u32 size_flags,
                                                       VMManager& vm_manager) {
    // TODO(Lioncache): Implement once the memory manager can handle this.
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleMapIOFlags(u32 flags, VMManager& vm_manager) {
    // TODO(Lioncache): Implement once the memory manager can handle this.
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleInterruptFlags(u32 flags) {
    constexpr u32 interrupt_ignore_value = 0x3FF;
    const u32 interrupt0 = (flags >> 12) & 0x3FF;
    const u32 interrupt1 = (flags >> 22) & 0x3FF;

    for (u32 interrupt : {interrupt0, interrupt1}) {
        if (interrupt == interrupt_ignore_value) {
            continue;
        }

        // NOTE:
        // This should be checking a generic interrupt controller value
        // as part of the calculation, however, given we don't currently
        // emulate that, it's sufficient to mark every interrupt as defined.

        if (interrupt >= interrupt_capabilities.size()) {
            return ERR_OUT_OF_RANGE;
        }

        interrupt_capabilities[interrupt] = true;
    }

    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleProgramTypeFlags(u32 flags) {
    const u32 reserved = flags >> 17;
    if (reserved != 0) {
        return ERR_RESERVED_VALUE;
    }

    program_type = static_cast<ProgramType>((flags >> 14) & 0b111);
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleKernelVersionFlags(u32 flags) {
    // Yes, the internal member variable is checked in the actual kernel here.
    // This might look odd for options that are only allowed to be initialized
    // just once, however the kernel has a separate initialization function for
    // kernel processes and userland processes. The kernel variant sets this
    // member variable ahead of time.

    const u32 major_version = kernel_version >> 19;

    if (major_version != 0 || flags < 0x80000) {
        return ERR_INVALID_CAPABILITY_DESCRIPTOR;
    }

    kernel_version = flags;
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleHandleTableFlags(u32 flags) {
    const u32 reserved = flags >> 26;
    if (reserved != 0) {
        return ERR_RESERVED_VALUE;
    }

    handle_table_size = static_cast<s32>((flags >> 16) & 0x3FF);
    return RESULT_SUCCESS;
}

ResultCode ProcessCapabilities::HandleDebugFlags(u32 flags) {
    const u32 reserved = flags >> 19;
    if (reserved != 0) {
        return ERR_RESERVED_VALUE;
    }

    is_debuggable = (flags & 0x20000) != 0;
    can_force_debug = (flags & 0x40000) != 0;
    return RESULT_SUCCESS;
}

} // namespace Kernel
