// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/result.h"

namespace Kernel {

// Confirmed Switch kernel error codes

constexpr ResultCode ERR_MAX_CONNECTIONS_REACHED{ErrorModule::Kernel, 7};
constexpr ResultCode ERR_INVALID_CAPABILITY_DESCRIPTOR{ErrorModule::Kernel, 14};
constexpr ResultCode ERR_INVALID_SIZE{ErrorModule::Kernel, 101};
constexpr ResultCode ERR_INVALID_ADDRESS{ErrorModule::Kernel, 102};
constexpr ResultCode ERR_OUT_OF_MEMORY{ErrorModule::Kernel, 104};
constexpr ResultCode ERR_HANDLE_TABLE_FULL{ErrorModule::Kernel, 105};
constexpr ResultCode ERR_INVALID_ADDRESS_STATE{ErrorModule::Kernel, 106};
constexpr ResultCode ERR_INVALID_MEMORY_PERMISSIONS{ErrorModule::Kernel, 108};
constexpr ResultCode ERR_INVALID_MEMORY_RANGE{ErrorModule::Kernel, 110};
constexpr ResultCode ERR_INVALID_PROCESSOR_ID{ErrorModule::Kernel, 113};
constexpr ResultCode ERR_INVALID_THREAD_PRIORITY{ErrorModule::Kernel, 112};
constexpr ResultCode ERR_INVALID_HANDLE{ErrorModule::Kernel, 114};
constexpr ResultCode ERR_INVALID_POINTER{ErrorModule::Kernel, 115};
constexpr ResultCode ERR_INVALID_COMBINATION{ErrorModule::Kernel, 116};
constexpr ResultCode RESULT_TIMEOUT{ErrorModule::Kernel, 117};
constexpr ResultCode ERR_SYNCHRONIZATION_CANCELED{ErrorModule::Kernel, 118};
constexpr ResultCode ERR_OUT_OF_RANGE{ErrorModule::Kernel, 119};
constexpr ResultCode ERR_INVALID_ENUM_VALUE{ErrorModule::Kernel, 120};
constexpr ResultCode ERR_NOT_FOUND{ErrorModule::Kernel, 121};
constexpr ResultCode ERR_BUSY{ErrorModule::Kernel, 122};
constexpr ResultCode ERR_SESSION_CLOSED_BY_REMOTE{ErrorModule::Kernel, 123};
constexpr ResultCode ERR_INVALID_STATE{ErrorModule::Kernel, 125};
constexpr ResultCode ERR_RESERVED_VALUE{ErrorModule::Kernel, 126};
constexpr ResultCode ERR_RESOURCE_LIMIT_EXCEEDED{ErrorModule::Kernel, 132};

} // namespace Kernel
