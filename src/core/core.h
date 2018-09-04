// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "common/common_types.h"
#include "core/hle/kernel/object.h"

namespace Core::Frontend {
class EmuWindow;
} // namespace Core::Frontend

namespace FileSys {
class VfsFilesystem;
} // namespace FileSys

namespace Kernel {
class KernelCore;
class Process;
class Scheduler;
} // namespace Kernel

namespace Loader {
class AppLoader;
enum class ResultStatus : u16;
} // namespace Loader

namespace Service::SM {
class ServiceManager;
} // namespace Service::SM

namespace Tegra {
class DebugContext;
class GPU;
} // namespace Tegra

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace Core {

class ARM_Interface;
class Cpu;
class ExclusiveMonitor;
class FrameLimiter;
class PerfStats;
class TelemetrySession;

struct PerfStatsResults;

class System {
public:
    System(const System&) = delete;
    System& operator=(const System&) = delete;

    System(System&&) = delete;
    System& operator=(System&&) = delete;

    ~System();

    /**
     * Gets the instance of the System singleton class.
     * @returns Reference to the instance of the System singleton class.
     */
    static System& GetInstance() {
        return s_instance;
    }

    /// Enumeration representing the return values of the System Initialize and Load process.
    enum class ResultStatus : u32 {
        Success,             ///< Succeeded
        ErrorNotInitialized, ///< Error trying to use core prior to initialization
        ErrorGetLoader,      ///< Error finding the correct application loader
        ErrorSystemMode,     ///< Error determining the system mode
        ErrorSystemFiles,    ///< Error in finding system files
        ErrorSharedFont,     ///< Error in finding shared font
        ErrorVideoCore,      ///< Error in the video core
        ErrorUnknown,        ///< Any other error
        ErrorLoader,         ///< The base for loader errors (too many to repeat)
    };

    /**
     * Run the core CPU loop
     * This function runs the core for the specified number of CPU instructions before trying to
     * update hardware. This is much faster than SingleStep (and should be equivalent), as the CPU
     * is not required to do a full dispatch with each instruction. NOTE: the number of instructions
     * requested is not guaranteed to run, as this will be interrupted preemptively if a hardware
     * update is requested (e.g. on a thread switch).
     * @param tight_loop If false, the CPU single-steps.
     * @return Result status, indicating whether or not the operation succeeded.
     */
    ResultStatus RunLoop(bool tight_loop = true);

    /**
     * Step the CPU one instruction
     * @return Result status, indicating whether or not the operation succeeded.
     */
    ResultStatus SingleStep();

    /**
     * Invalidate the CPU instruction caches
     * This function should only be used by GDB Stub to support breakpoints, memory updates and
     * step/continue commands.
     */
    void InvalidateCpuInstructionCaches();

    /// Shutdown the emulated system.
    void Shutdown();

    /**
     * Load an executable application.
     * @param emu_window Reference to the host-system window used for video output and keyboard
     *                   input.
     * @param filepath String path to the executable application to load on the host file system.
     * @returns ResultStatus code, indicating if the operation succeeded.
     */
    ResultStatus Load(Frontend::EmuWindow& emu_window, const std::string& filepath);

    /**
     * Indicates if the emulated system is powered on (all subsystems initialized and able to run an
     * application).
     * @returns True if the emulated system is powered on, otherwise false.
     */
    bool IsPoweredOn() const;

    /**
     * Returns a reference to the telemetry session for this emulation session.
     * @returns Reference to the telemetry session.
     */
    Core::TelemetrySession& TelemetrySession() const;

    /// Prepare the core emulation for a reschedule
    void PrepareReschedule();

    /// Gets and resets core performance statistics
    PerfStatsResults GetAndResetPerfStats();

    /// Gets an ARM interface to the CPU core that is currently running
    ARM_Interface& CurrentArmInterface();

    /// Gets the index of the currently running CPU core
    size_t CurrentCoreIndex();

    /// Gets the scheduler for the CPU core that is currently running
    Kernel::Scheduler& CurrentScheduler();

    /// Gets an ARM interface to the CPU core with the specified index
    ARM_Interface& ArmInterface(size_t core_index);

    /// Gets a CPU interface to the CPU core with the specified index
    Cpu& CpuCore(size_t core_index);

    /// Gets the exclusive monitor
    ExclusiveMonitor& Monitor();

    /// Gets a mutable reference to the GPU interface
    Tegra::GPU& GPU();

    /// Gets an immutable reference to the GPU interface.
    const Tegra::GPU& GPU() const;

    /// Gets a mutable reference to the renderer.
    VideoCore::RendererBase& Renderer();

    /// Gets an immutable reference to the renderer.
    const VideoCore::RendererBase& Renderer() const;

    /// Gets the scheduler for the CPU core with the specified index
    const std::shared_ptr<Kernel::Scheduler>& Scheduler(size_t core_index);

    /// Gets the current process
    Kernel::SharedPtr<Kernel::Process>& CurrentProcess();

    /// Provides a reference to the kernel instance.
    Kernel::KernelCore& Kernel();

    /// Provides a constant reference to the kernel instance.
    const Kernel::KernelCore& Kernel() const;

    /// Provides a reference to the internal PerfStats instance.
    Core::PerfStats& GetPerfStats();

    /// Provides a constant reference to the internal PerfStats instance.
    const Core::PerfStats& GetPerfStats() const;

    /// Provides a reference to the frame limiter;
    Core::FrameLimiter& FrameLimiter();

    /// Provides a constant referent to the frame limiter
    const Core::FrameLimiter& FrameLimiter() const;

    /// Gets the name of the current game
    Loader::ResultStatus GetGameName(std::string& out) const;

    void SetStatus(ResultStatus new_status, const char* details);

    const std::string& GetStatusDetails() const;

    Loader::AppLoader& GetAppLoader() const;

    Service::SM::ServiceManager& ServiceManager();
    const Service::SM::ServiceManager& ServiceManager() const;

    void SetGPUDebugContext(std::shared_ptr<Tegra::DebugContext> context);

    Tegra::DebugContext* GetGPUDebugContext() const;

    void SetFilesystem(std::shared_ptr<FileSys::VfsFilesystem> vfs);

    std::shared_ptr<FileSys::VfsFilesystem> GetFilesystem() const;

private:
    System();

    /// Returns the currently running CPU core
    Cpu& CurrentCpuCore();

    /**
     * Initialize the emulated system.
     * @param emu_window Reference to the host-system window used for video output and keyboard
     *                   input.
     * @return ResultStatus code, indicating if the operation succeeded.
     */
    ResultStatus Init(Frontend::EmuWindow& emu_window);

    struct Impl;
    std::unique_ptr<Impl> impl;

    static System s_instance;
};

inline ARM_Interface& CurrentArmInterface() {
    return System::GetInstance().CurrentArmInterface();
}

inline TelemetrySession& Telemetry() {
    return System::GetInstance().TelemetrySession();
}

inline Kernel::SharedPtr<Kernel::Process>& CurrentProcess() {
    return System::GetInstance().CurrentProcess();
}

} // namespace Core
