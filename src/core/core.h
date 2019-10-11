// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "common/common_types.h"
#include "core/file_sys/vfs_types.h"
#include "core/hle/kernel/object.h"

namespace Core::Frontend {
class EmuWindow;
} // namespace Core::Frontend

namespace FileSys {
class ContentProvider;
class ContentProviderUnion;
enum class ContentProviderUnionSlot;
class VfsFilesystem;
} // namespace FileSys

namespace Kernel {
class GlobalScheduler;
class KernelCore;
class Process;
class Scheduler;
} // namespace Kernel

namespace Loader {
class AppLoader;
enum class ResultStatus : u16;
} // namespace Loader

namespace Memory {
struct CheatEntry;
} // namespace Memory

namespace Service {

namespace AM::Applets {
struct AppletFrontendSet;
class AppletManager;
} // namespace AM::Applets

namespace APM {
class Controller;
}

namespace FileSystem {
class FileSystemController;
} // namespace FileSystem

namespace Glue {
class ARPManager;
}

namespace LM {
class Manager;
} // namespace LM

namespace SM {
class ServiceManager;
} // namespace SM

} // namespace Service

namespace Tegra {
class DebugContext;
class GPU;
} // namespace Tegra

namespace VideoCore {
class RendererBase;
} // namespace VideoCore

namespace Core::Timing {
class CoreTiming;
}

namespace Core::Hardware {
class InterruptManager;
}

namespace Core {

class ARM_Interface;
class Cpu;
class ExclusiveMonitor;
class FrameLimiter;
class PerfStats;
class Reporter;
class TelemetrySession;

struct PerfStatsResults;

FileSys::VirtualFile GetGameFileFromPath(const FileSys::VirtualFilesystem& vfs,
                                         const std::string& path);

class System {
public:
    using CurrentBuildProcessID = std::array<u8, 0x20>;

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

    /// Gets a reference to the telemetry session for this emulation session.
    Core::TelemetrySession& TelemetrySession();

    /// Gets a reference to the telemetry session for this emulation session.
    const Core::TelemetrySession& TelemetrySession() const;

    /// Prepare the core emulation for a reschedule
    void PrepareReschedule();

    /// Prepare the core emulation for a reschedule
    void PrepareReschedule(u32 core_index);

    /// Gets and resets core performance statistics
    PerfStatsResults GetAndResetPerfStats();

    /// Gets an ARM interface to the CPU core that is currently running
    ARM_Interface& CurrentArmInterface();

    /// Gets an ARM interface to the CPU core that is currently running
    const ARM_Interface& CurrentArmInterface() const;

    /// Gets the index of the currently running CPU core
    std::size_t CurrentCoreIndex() const;

    /// Gets the scheduler for the CPU core that is currently running
    Kernel::Scheduler& CurrentScheduler();

    /// Gets the scheduler for the CPU core that is currently running
    const Kernel::Scheduler& CurrentScheduler() const;

    /// Gets a reference to an ARM interface for the CPU core with the specified index
    ARM_Interface& ArmInterface(std::size_t core_index);

    /// Gets a const reference to an ARM interface from the CPU core with the specified index
    const ARM_Interface& ArmInterface(std::size_t core_index) const;

    /// Gets a CPU interface to the CPU core with the specified index
    Cpu& CpuCore(std::size_t core_index);

    /// Gets a CPU interface to the CPU core with the specified index
    const Cpu& CpuCore(std::size_t core_index) const;

    /// Gets a reference to the exclusive monitor
    ExclusiveMonitor& Monitor();

    /// Gets a constant reference to the exclusive monitor
    const ExclusiveMonitor& Monitor() const;

    /// Gets a mutable reference to the GPU interface
    Tegra::GPU& GPU();

    /// Gets an immutable reference to the GPU interface.
    const Tegra::GPU& GPU() const;

    /// Gets a mutable reference to the renderer.
    VideoCore::RendererBase& Renderer();

    /// Gets an immutable reference to the renderer.
    const VideoCore::RendererBase& Renderer() const;

    /// Gets the scheduler for the CPU core with the specified index
    Kernel::Scheduler& Scheduler(std::size_t core_index);

    /// Gets the scheduler for the CPU core with the specified index
    const Kernel::Scheduler& Scheduler(std::size_t core_index) const;

    /// Gets the global scheduler
    Kernel::GlobalScheduler& GlobalScheduler();

    /// Gets the global scheduler
    const Kernel::GlobalScheduler& GlobalScheduler() const;

    /// Provides a pointer to the current process
    Kernel::Process* CurrentProcess();

    /// Provides a constant pointer to the current process.
    const Kernel::Process* CurrentProcess() const;

    /// Provides a reference to the core timing instance.
    Timing::CoreTiming& CoreTiming();

    /// Provides a constant reference to the core timing instance.
    const Timing::CoreTiming& CoreTiming() const;

    /// Provides a reference to the interrupt manager instance.
    Core::Hardware::InterruptManager& InterruptManager();

    /// Provides a constant reference to the interrupt manager instance.
    const Core::Hardware::InterruptManager& InterruptManager() const;

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

    void RegisterCheatList(const std::vector<Memory::CheatEntry>& list,
                           const std::array<u8, 0x20>& build_id, VAddr main_region_begin,
                           u64 main_region_size);

    void SetAppletFrontendSet(Service::AM::Applets::AppletFrontendSet&& set);

    void SetDefaultAppletFrontendSet();

    Service::AM::Applets::AppletManager& GetAppletManager();

    const Service::AM::Applets::AppletManager& GetAppletManager() const;

    void SetContentProvider(std::unique_ptr<FileSys::ContentProviderUnion> provider);

    FileSys::ContentProvider& GetContentProvider();

    const FileSys::ContentProvider& GetContentProvider() const;

    Service::FileSystem::FileSystemController& GetFileSystemController();

    const Service::FileSystem::FileSystemController& GetFileSystemController() const;

    void RegisterContentProvider(FileSys::ContentProviderUnionSlot slot,
                                 FileSys::ContentProvider* provider);

    void ClearContentProvider(FileSys::ContentProviderUnionSlot slot);

    const Reporter& GetReporter() const;

    Service::Glue::ARPManager& GetARPManager();

    const Service::Glue::ARPManager& GetARPManager() const;

    Service::APM::Controller& GetAPMController();

    const Service::APM::Controller& GetAPMController() const;

    Service::LM::Manager& GetLogManager();

    const Service::LM::Manager& GetLogManager() const;

    void SetExitLock(bool locked);

    bool GetExitLock() const;

    void SetCurrentProcessBuildID(const CurrentBuildProcessID& id);

    const CurrentBuildProcessID& GetCurrentProcessBuildID() const;

private:
    System();

    /// Returns the currently running CPU core
    Cpu& CurrentCpuCore();

    /// Returns the currently running CPU core
    const Cpu& CurrentCpuCore() const;

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

} // namespace Core
