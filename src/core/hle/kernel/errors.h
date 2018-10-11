// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/result.h"

namespace Kernel {

namespace ErrCodes {
enum {
    // TODO(Subv): Remove these 3DS OS error codes.
    SessionClosedByRemote = 26,
    NoPendingSessions = 35,
    InvalidBufferDescriptor = 48,

    // Confirmed Switch OS error codes
    MaxConnectionsReached = 7,
    InvalidSize = 101,
    InvalidAddress = 102,
    HandleTableFull = 105,
    InvalidMemoryState = 106,
    InvalidMemoryPermissions = 108,
    InvalidMemoryRange = 110,
    InvalidThreadPriority = 112,
    InvalidProcessorId = 113,
    InvalidHandle = 114,
    InvalidCombination = 116,
    Timeout = 117,
    SynchronizationCanceled = 118,
    TooLarge = 119,
    InvalidEnumValue = 120,
    NoSuchEntry = 121,
    AlreadyRegistered = 122,
    InvalidState = 125,
    ResourceLimitExceeded = 132,
};
}

// WARNING: The kernel is quite inconsistent in it's usage of errors code. Make sure to always
// double check that the code matches before re-using the constant.

// TODO(bunnei): Replace -1 with correct errors for Switch OS
constexpr ResultCode ERR_HANDLE_TABLE_FULL(ErrorModule::Kernel, ErrCodes::HandleTableFull);
constexpr ResultCode ERR_SESSION_CLOSED_BY_REMOTE(-1);
constexpr ResultCode ERR_PORT_NAME_TOO_LONG(ErrorModule::Kernel, ErrCodes::TooLarge);
constexpr ResultCode ERR_MAX_CONNECTIONS_REACHED(ErrorModule::Kernel,
                                                 ErrCodes::MaxConnectionsReached);
constexpr ResultCode ERR_INVALID_ENUM_VALUE(ErrorModule::Kernel, ErrCodes::InvalidEnumValue);
constexpr ResultCode ERR_INVALID_ENUM_VALUE_FND(-1);
constexpr ResultCode ERR_INVALID_COMBINATION(-1);
constexpr ResultCode ERR_INVALID_COMBINATION_KERNEL(ErrorModule::Kernel,
                                                    ErrCodes::InvalidCombination);
constexpr ResultCode ERR_OUT_OF_MEMORY(-1);
constexpr ResultCode ERR_INVALID_ADDRESS(ErrorModule::Kernel, ErrCodes::InvalidAddress);
constexpr ResultCode ERR_INVALID_ADDRESS_STATE(ErrorModule::Kernel, ErrCodes::InvalidMemoryState);
constexpr ResultCode ERR_INVALID_MEMORY_PERMISSIONS(ErrorModule::Kernel,
                                                    ErrCodes::InvalidMemoryPermissions);
constexpr ResultCode ERR_INVALID_MEMORY_RANGE(ErrorModule::Kernel, ErrCodes::InvalidMemoryRange);
constexpr ResultCode ERR_INVALID_HANDLE(ErrorModule::Kernel, ErrCodes::InvalidHandle);
constexpr ResultCode ERR_INVALID_PROCESSOR_ID(ErrorModule::Kernel, ErrCodes::InvalidProcessorId);
constexpr ResultCode ERR_INVALID_SIZE(ErrorModule::Kernel, ErrCodes::InvalidSize);
constexpr ResultCode ERR_ALREADY_REGISTERED(ErrorModule::Kernel, ErrCodes::AlreadyRegistered);
constexpr ResultCode ERR_INVALID_STATE(ErrorModule::Kernel, ErrCodes::InvalidState);
constexpr ResultCode ERR_INVALID_THREAD_PRIORITY(ErrorModule::Kernel,
                                                 ErrCodes::InvalidThreadPriority);
constexpr ResultCode ERR_INVALID_POINTER(-1);
constexpr ResultCode ERR_INVALID_OBJECT_ADDR(-1);
constexpr ResultCode ERR_NOT_AUTHORIZED(-1);
/// Alternate code returned instead of ERR_INVALID_HANDLE in some code paths.
constexpr ResultCode ERR_INVALID_HANDLE_OS(-1);
constexpr ResultCode ERR_NOT_FOUND(ErrorModule::Kernel, ErrCodes::NoSuchEntry);
constexpr ResultCode RESULT_TIMEOUT(ErrorModule::Kernel, ErrCodes::Timeout);
/// Returned when Accept() is called on a port with no sessions to be accepted.
constexpr ResultCode ERR_NO_PENDING_SESSIONS(-1);

} // namespace Kernel
