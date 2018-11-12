// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <map>
#include <memory>
#include <thread>
#include <utility>

#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/arm/exclusive_monitor.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/file_sys/mode.h"
#include "core/file_sys/vfs_concat.h"
#include "core/file_sys/vfs_real.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/am/applets/software_keyboard.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/loader/loader.h"
#include "core/perf_stats.h"
#include "core/settings.h"
#include "core/telemetry_session.h"
#include "frontend/applets/software_keyboard.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Core {

/*static*/ System System::s_instance;

namespace {
FileSys::VirtualFile GetGameFileFromPath(const FileSys::VirtualFilesystem& vfs,
                                         const std::string& path) {
    // To account for split 00+01+etc files.
    std::string dir_name;
    std::string filename;
    Common::SplitPath(path, &dir_name, &filename, nullptr);
    if (filename == "00") {
        const auto dir = vfs->OpenDirectory(dir_name, FileSys::Mode::Read);
        std::vector<FileSys::VirtualFile> concat;
        for (u8 i = 0; i < 0x10; ++i) {
            auto next = dir->GetFile(fmt::format("{:02X}", i));
            if (next != nullptr)
                concat.push_back(std::move(next));
            else {
                next = dir->GetFile(fmt::format("{:02x}", i));
                if (next != nullptr)
                    concat.push_back(std::move(next));
                else
                    break;
            }
        }

        if (concat.empty())
            return nullptr;

        return FileSys::ConcatenatedVfsFile::MakeConcatenatedFile(concat, dir->GetName());
    }

    return vfs->OpenFile(path, FileSys::Mode::Read);
}

/// Runs a CPU core while the system is powered on
void RunCpuCore(Cpu& cpu_state) {
    while (Core::System::GetInstance().IsPoweredOn()) {
        cpu_state.RunLoop(true);
    }
}
} // Anonymous namespace

struct System::Impl {
    Cpu& CurrentCpuCore() {
        if (Settings::values.use_multi_core) {
            const auto& search = thread_to_cpu.find(std::this_thread::get_id());
            ASSERT(search != thread_to_cpu.end());
            ASSERT(search->second);
            return *search->second;
        }

        // Otherwise, use single-threaded mode active_core variable
        return *cpu_cores[active_core];
    }

    ResultStatus RunLoop(bool tight_loop) {
        status = ResultStatus::Success;

        // Update thread_to_cpu in case Core 0 is run from a different host thread
        thread_to_cpu[std::this_thread::get_id()] = cpu_cores[0].get();

        if (GDBStub::IsServerEnabled()) {
            GDBStub::HandlePacket();

            // If the loop is halted and we want to step, use a tiny (1) number of instructions to
            // execute. Otherwise, get out of the loop function.
            if (GDBStub::GetCpuHaltFlag()) {
                if (GDBStub::GetCpuStepFlag()) {
                    tight_loop = false;
                } else {
                    return ResultStatus::Success;
                }
            }
        }

        for (active_core = 0; active_core < NUM_CPU_CORES; ++active_core) {
            cpu_cores[active_core]->RunLoop(tight_loop);
            if (Settings::values.use_multi_core) {
                // Cores 1-3 are run on other threads in this mode
                break;
            }
        }

        if (GDBStub::IsServerEnabled()) {
            GDBStub::SetCpuStepFlag(false);
        }

        return status;
    }

    ResultStatus Init(Frontend::EmuWindow& emu_window) {
        LOG_DEBUG(HW_Memory, "initialized OK");

        CoreTiming::Init();
        kernel.Initialize();

        // Create a default fs if one doesn't already exist.
        if (virtual_filesystem == nullptr)
            virtual_filesystem = std::make_shared<FileSys::RealVfsFilesystem>();

        /// Create default implementations of applets if one is not provided.
        if (software_keyboard == nullptr)
            software_keyboard = std::make_unique<Core::Frontend::DefaultSoftwareKeyboardApplet>();

        auto main_process = Kernel::Process::Create(kernel, "main");
        kernel.MakeCurrentProcess(main_process.get());

        cpu_barrier = std::make_unique<CpuBarrier>();
        cpu_exclusive_monitor = Cpu::MakeExclusiveMonitor(cpu_cores.size());
        for (std::size_t index = 0; index < cpu_cores.size(); ++index) {
            cpu_cores[index] = std::make_unique<Cpu>(*cpu_exclusive_monitor, *cpu_barrier, index);
        }

        telemetry_session = std::make_unique<Core::TelemetrySession>();
        service_manager = std::make_shared<Service::SM::ServiceManager>();

        Service::Init(service_manager, *virtual_filesystem);
        GDBStub::Init();

        renderer = VideoCore::CreateRenderer(emu_window);
        if (!renderer->Init()) {
            return ResultStatus::ErrorVideoCore;
        }

        gpu_core = std::make_unique<Tegra::GPU>(renderer->Rasterizer());

        // Create threads for CPU cores 1-3, and build thread_to_cpu map
        // CPU core 0 is run on the main thread
        thread_to_cpu[std::this_thread::get_id()] = cpu_cores[0].get();
        if (Settings::values.use_multi_core) {
            for (std::size_t index = 0; index < cpu_core_threads.size(); ++index) {
                cpu_core_threads[index] =
                    std::make_unique<std::thread>(RunCpuCore, std::ref(*cpu_cores[index + 1]));
                thread_to_cpu[cpu_core_threads[index]->get_id()] = cpu_cores[index + 1].get();
            }
        }

        LOG_DEBUG(Core, "Initialized OK");

        // Reset counters and set time origin to current frame
        GetAndResetPerfStats();
        perf_stats.BeginSystemFrame();

        return ResultStatus::Success;
    }

    ResultStatus Load(Frontend::EmuWindow& emu_window, const std::string& filepath) {
        app_loader = Loader::GetLoader(GetGameFileFromPath(virtual_filesystem, filepath));

        if (!app_loader) {
            LOG_CRITICAL(Core, "Failed to obtain loader for {}!", filepath);
            return ResultStatus::ErrorGetLoader;
        }
        std::pair<std::optional<u32>, Loader::ResultStatus> system_mode =
            app_loader->LoadKernelSystemMode();

        if (system_mode.second != Loader::ResultStatus::Success) {
            LOG_CRITICAL(Core, "Failed to determine system mode (Error {})!",
                         static_cast<int>(system_mode.second));

            return ResultStatus::ErrorSystemMode;
        }

        ResultStatus init_result{Init(emu_window)};
        if (init_result != ResultStatus::Success) {
            LOG_CRITICAL(Core, "Failed to initialize system (Error {})!",
                         static_cast<int>(init_result));
            Shutdown();
            return init_result;
        }

        const Loader::ResultStatus load_result{app_loader->Load(*kernel.CurrentProcess())};
        if (load_result != Loader::ResultStatus::Success) {
            LOG_CRITICAL(Core, "Failed to load ROM (Error {})!", static_cast<int>(load_result));
            Shutdown();

            return static_cast<ResultStatus>(static_cast<u32>(ResultStatus::ErrorLoader) +
                                             static_cast<u32>(load_result));
        }
        status = ResultStatus::Success;
        return status;
    }

    void Shutdown() {
        // Log last frame performance stats
        auto perf_results = GetAndResetPerfStats();
        Telemetry().AddField(Telemetry::FieldType::Performance, "Shutdown_EmulationSpeed",
                             perf_results.emulation_speed * 100.0);
        Telemetry().AddField(Telemetry::FieldType::Performance, "Shutdown_Framerate",
                             perf_results.game_fps);
        Telemetry().AddField(Telemetry::FieldType::Performance, "Shutdown_Frametime",
                             perf_results.frametime * 1000.0);

        // Shutdown emulation session
        renderer.reset();
        GDBStub::Shutdown();
        Service::Shutdown();
        service_manager.reset();
        telemetry_session.reset();
        gpu_core.reset();

        // Close all CPU/threading state
        cpu_barrier->NotifyEnd();
        if (Settings::values.use_multi_core) {
            for (auto& thread : cpu_core_threads) {
                thread->join();
                thread.reset();
            }
        }
        thread_to_cpu.clear();
        for (auto& cpu_core : cpu_cores) {
            cpu_core.reset();
        }
        cpu_exclusive_monitor.reset();
        cpu_barrier.reset();

        // Shutdown kernel and core timing
        kernel.Shutdown();
        CoreTiming::Shutdown();

        // Close app loader
        app_loader.reset();

        LOG_DEBUG(Core, "Shutdown OK");
    }

    Loader::ResultStatus GetGameName(std::string& out) const {
        if (app_loader == nullptr)
            return Loader::ResultStatus::ErrorNotInitialized;
        return app_loader->ReadTitle(out);
    }

    void SetStatus(ResultStatus new_status, const char* details = nullptr) {
        status = new_status;
        if (details) {
            status_details = details;
        }
    }

    PerfStatsResults GetAndResetPerfStats() {
        return perf_stats.GetAndResetStats(CoreTiming::GetGlobalTimeUs());
    }

    Kernel::KernelCore kernel;
    /// RealVfsFilesystem instance
    FileSys::VirtualFilesystem virtual_filesystem;
    /// AppLoader used to load the current executing application
    std::unique_ptr<Loader::AppLoader> app_loader;
    std::unique_ptr<VideoCore::RendererBase> renderer;
    std::unique_ptr<Tegra::GPU> gpu_core;
    std::shared_ptr<Tegra::DebugContext> debug_context;
    std::unique_ptr<ExclusiveMonitor> cpu_exclusive_monitor;
    std::unique_ptr<CpuBarrier> cpu_barrier;
    std::array<std::unique_ptr<Cpu>, NUM_CPU_CORES> cpu_cores;
    std::array<std::unique_ptr<std::thread>, NUM_CPU_CORES - 1> cpu_core_threads;
    std::size_t active_core{}; ///< Active core, only used in single thread mode

    /// Frontend applets
    std::unique_ptr<Core::Frontend::SoftwareKeyboardApplet> software_keyboard;

    /// Service manager
    std::shared_ptr<Service::SM::ServiceManager> service_manager;

    /// Telemetry session for this emulation session
    std::unique_ptr<Core::TelemetrySession> telemetry_session;

    ResultStatus status = ResultStatus::Success;
    std::string status_details = "";

    /// Map of guest threads to CPU cores
    std::map<std::thread::id, Cpu*> thread_to_cpu;

    Core::PerfStats perf_stats;
    Core::FrameLimiter frame_limiter;
};

System::System() : impl{std::make_unique<Impl>()} {}
System::~System() = default;

Cpu& System::CurrentCpuCore() {
    return impl->CurrentCpuCore();
}

const Cpu& System::CurrentCpuCore() const {
    return impl->CurrentCpuCore();
}

System::ResultStatus System::RunLoop(bool tight_loop) {
    return impl->RunLoop(tight_loop);
}

System::ResultStatus System::SingleStep() {
    return RunLoop(false);
}

void System::InvalidateCpuInstructionCaches() {
    for (auto& cpu : impl->cpu_cores) {
        cpu->ArmInterface().ClearInstructionCache();
    }
}

System::ResultStatus System::Load(Frontend::EmuWindow& emu_window, const std::string& filepath) {
    return impl->Load(emu_window, filepath);
}

bool System::IsPoweredOn() const {
    return impl->cpu_barrier && impl->cpu_barrier->IsAlive();
}

void System::PrepareReschedule() {
    CurrentCpuCore().PrepareReschedule();
}

PerfStatsResults System::GetAndResetPerfStats() {
    return impl->GetAndResetPerfStats();
}

TelemetrySession& System::TelemetrySession() {
    return *impl->telemetry_session;
}

const TelemetrySession& System::TelemetrySession() const {
    return *impl->telemetry_session;
}

ARM_Interface& System::CurrentArmInterface() {
    return CurrentCpuCore().ArmInterface();
}

const ARM_Interface& System::CurrentArmInterface() const {
    return CurrentCpuCore().ArmInterface();
}

std::size_t System::CurrentCoreIndex() const {
    return CurrentCpuCore().CoreIndex();
}

Kernel::Scheduler& System::CurrentScheduler() {
    return CurrentCpuCore().Scheduler();
}

const Kernel::Scheduler& System::CurrentScheduler() const {
    return CurrentCpuCore().Scheduler();
}

Kernel::Scheduler& System::Scheduler(std::size_t core_index) {
    return CpuCore(core_index).Scheduler();
}

const Kernel::Scheduler& System::Scheduler(std::size_t core_index) const {
    return CpuCore(core_index).Scheduler();
}

Kernel::Process* System::CurrentProcess() {
    return impl->kernel.CurrentProcess();
}

const Kernel::Process* System::CurrentProcess() const {
    return impl->kernel.CurrentProcess();
}

ARM_Interface& System::ArmInterface(std::size_t core_index) {
    return CpuCore(core_index).ArmInterface();
}

const ARM_Interface& System::ArmInterface(std::size_t core_index) const {
    return CpuCore(core_index).ArmInterface();
}

Cpu& System::CpuCore(std::size_t core_index) {
    ASSERT(core_index < NUM_CPU_CORES);
    return *impl->cpu_cores[core_index];
}

const Cpu& System::CpuCore(std::size_t core_index) const {
    ASSERT(core_index < NUM_CPU_CORES);
    return *impl->cpu_cores[core_index];
}

ExclusiveMonitor& System::Monitor() {
    return *impl->cpu_exclusive_monitor;
}

const ExclusiveMonitor& System::Monitor() const {
    return *impl->cpu_exclusive_monitor;
}

Tegra::GPU& System::GPU() {
    return *impl->gpu_core;
}

const Tegra::GPU& System::GPU() const {
    return *impl->gpu_core;
}

VideoCore::RendererBase& System::Renderer() {
    return *impl->renderer;
}

const VideoCore::RendererBase& System::Renderer() const {
    return *impl->renderer;
}

Kernel::KernelCore& System::Kernel() {
    return impl->kernel;
}

const Kernel::KernelCore& System::Kernel() const {
    return impl->kernel;
}

Core::PerfStats& System::GetPerfStats() {
    return impl->perf_stats;
}

const Core::PerfStats& System::GetPerfStats() const {
    return impl->perf_stats;
}

Core::FrameLimiter& System::FrameLimiter() {
    return impl->frame_limiter;
}

const Core::FrameLimiter& System::FrameLimiter() const {
    return impl->frame_limiter;
}

Loader::ResultStatus System::GetGameName(std::string& out) const {
    return impl->GetGameName(out);
}

void System::SetStatus(ResultStatus new_status, const char* details) {
    impl->SetStatus(new_status, details);
}

const std::string& System::GetStatusDetails() const {
    return impl->status_details;
}

Loader::AppLoader& System::GetAppLoader() const {
    return *impl->app_loader;
}

void System::SetGPUDebugContext(std::shared_ptr<Tegra::DebugContext> context) {
    impl->debug_context = std::move(context);
}

Tegra::DebugContext* System::GetGPUDebugContext() const {
    return impl->debug_context.get();
}

void System::SetFilesystem(std::shared_ptr<FileSys::VfsFilesystem> vfs) {
    impl->virtual_filesystem = std::move(vfs);
}

std::shared_ptr<FileSys::VfsFilesystem> System::GetFilesystem() const {
    return impl->virtual_filesystem;
}

void System::SetSoftwareKeyboard(std::unique_ptr<Core::Frontend::SoftwareKeyboardApplet> applet) {
    impl->software_keyboard = std::move(applet);
}

const Core::Frontend::SoftwareKeyboardApplet& System::GetSoftwareKeyboard() const {
    return *impl->software_keyboard;
}

System::ResultStatus System::Init(Frontend::EmuWindow& emu_window) {
    return impl->Init(emu_window);
}

void System::Shutdown() {
    impl->Shutdown();
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *impl->service_manager;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *impl->service_manager;
}

} // namespace Core
