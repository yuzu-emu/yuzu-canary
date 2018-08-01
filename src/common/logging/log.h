// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <fmt/format.h>
#include "common/common_types.h"

namespace Log {

/// Specifies the severity or level of detail of the log message.
enum class Level : u8 {
    Trace,    ///< Extremely detailed and repetitive debugging information that is likely to
              ///  pollute logs.
    Debug,    ///< Less detailed debugging information.
    Info,     ///< Status information from important points during execution.
    Warning,  ///< Minor or potential problems found during execution of a task.
    Error,    ///< Major problems found during execution of a task that prevent it from being
              ///  completed.
    Critical, ///< Major problems during execution that threathen the stability of the entire
              ///  application.

    Count ///< Total number of logging levels
};

typedef u8 ClassType;

/**
 * Specifies the sub-system that generated the log message.
 *
 * @note If you add a new entry here, also add a corresponding one to `ALL_LOG_CLASSES` in
 * backend.cpp.
 */
enum class Class : ClassType {
    Log,               ///< Messages about the log system itself
    Common,            ///< Library routines
    Common_Filesystem, ///< Filesystem interface library
    Common_Memory,     ///< Memory mapping and management functions
    Core,              ///< LLE emulation core
    Core_ARM,          ///< ARM CPU core
    Core_Timing,       ///< CoreTiming functions
    Config,            ///< Emulator configuration (including commandline)
    Debug,             ///< Debugging tools
    Debug_Emulated,    ///< Debug messages from the emulated programs
    Debug_GPU,         ///< GPU debugging tools
    Debug_Breakpoint,  ///< Logging breakpoints and watchpoints
    Debug_GDBStub,     ///< GDB Stub
    Kernel,            ///< The HLE implementation of the CTR kernel
    Kernel_SVC,        ///< Kernel system calls
    Service,           ///< HLE implementation of system services. Each major service
                       ///  should have its own subclass.
    Service_ACC,       ///< The ACC (Accounts) service
    Service_AM,        ///< The AM (Applet manager) service
    Service_AOC,       ///< The AOC (AddOn Content) service
    Service_APM,       ///< The APM (Performance) service
    Service_Audio,     ///< The Audio (Audio control) service
    Service_BCAT,      ///< The BCAT service
    Service_BTM,       ///< The BTM service
    Service_Fatal,     ///< The Fatal service
    Service_FGM,       ///< The FGM service
    Service_Friend,    ///< The friend service
    Service_FS,        ///< The FS (Filesystem) service
    Service_HID,       ///< The HID (Human interface device) service
    Service_LBL,       ///< The LBL (LCD backlight) service
    Service_LDN,       ///< The LDN (Local domain network) service
    Service_LM,        ///< The LM (Logger) service
    Service_Mii,       ///< The Mii service
    Service_MM,        ///< The MM (Multimedia) service
    Service_NCM,       ///< The NCM service
    Service_NFC,       ///< The NFC (Near-field communication) service
    Service_NFP,       ///< The NFP service
    Service_NIFM,      ///< The NIFM (Network interface) service
    Service_NS,        ///< The NS services
    Service_NVDRV,     ///< The NVDRV (Nvidia driver) service
    Service_PCIE,      ///< The PCIe service
    Service_PCTL,      ///< The PCTL (Parental control) service
    Service_PREPO,     ///< The PREPO (Play report) service
    Service_SET,       ///< The SET (Settings) service
    Service_SM,        ///< The SM (Service manager) service
    Service_SPL,       ///< The SPL service
    Service_SSL,       ///< The SSL service
    Service_Time,      ///< The time service
    Service_VI,        ///< The VI (Video interface) service
    Service_WLAN,      ///< The WLAN (Wireless local area network) service
    HW,                ///< Low-level hardware emulation
    HW_Memory,         ///< Memory-map and address translation
    HW_LCD,            ///< LCD register emulation
    HW_GPU,            ///< GPU control emulation
    HW_AES,            ///< AES engine emulation
    IPC,               ///< IPC interface
    Frontend,          ///< Emulator UI
    Render,            ///< Emulator video output and hardware acceleration
    Render_Software,   ///< Software renderer backend
    Render_OpenGL,     ///< OpenGL backend
    Audio,             ///< Audio emulation
    Audio_DSP,         ///< The HLE implementation of the DSP
    Audio_Sink,        ///< Emulator audio output backend
    Loader,            ///< ROM loader
    Crypto,            ///< Cryptographic engine/functions
    Input,             ///< Input emulation
    Network,           ///< Network emulation
    WebService,        ///< Interface to yuzu Web Services
    Count              ///< Total number of logging classes
};

/// Logs a message to the global logger, using fmt
void FmtLogMessageImpl(Class log_class, Level log_level, const char* filename,
                       unsigned int line_num, const char* function, const char* format,
                       const fmt::format_args& args);

template <typename... Args>
void FmtLogMessage(Class log_class, Level log_level, const char* filename, unsigned int line_num,
                   const char* function, const char* format, const Args&... args) {
    FmtLogMessageImpl(log_class, log_level, filename, line_num, function, format,
                      fmt::make_format_args(args...));
}

} // namespace Log

#ifdef _DEBUG
#define LOG_TRACE(log_class, ...)                                                                  \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Trace, __FILE__, __LINE__,         \
                         __func__, __VA_ARGS__)
#else
#define LOG_TRACE(log_class, fmt, ...) (void(0))
#endif

#define LOG_DEBUG(log_class, ...)                                                                  \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Debug, __FILE__, __LINE__,         \
                         __func__, __VA_ARGS__)
#define LOG_INFO(log_class, ...)                                                                   \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Info, __FILE__, __LINE__,          \
                         __func__, __VA_ARGS__)
#define LOG_WARNING(log_class, ...)                                                                \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Warning, __FILE__, __LINE__,       \
                         __func__, __VA_ARGS__)
#define LOG_ERROR(log_class, ...)                                                                  \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Error, __FILE__, __LINE__,         \
                         __func__, __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...)                                                               \
    ::Log::FmtLogMessage(::Log::Class::log_class, ::Log::Level::Critical, __FILE__, __LINE__,      \
                         __func__, __VA_ARGS__)
