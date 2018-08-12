// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <utility>
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/controller.h"
#include "core/hle/service/sm/sm.h"
#include "core/loader/loader.h"
#include "core/settings.h"
#include "file_sys/vfs_concat.h"
#include "file_sys/vfs_real.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

namespace Core {

/*static*/ System System::s_instance;

System::System() = default;

System::~System() = default;

/// Runs a CPU core while the system is powered on
static void RunCpuCore(std::shared_ptr<Cpu> cpu_state) {
    while (Core::System::GetInstance().IsPoweredOn()) {
        cpu_state->RunLoop(true);
    }
}

Cpu& System::CurrentCpuCore() {
    // If multicore is enabled, use host thread to figure out the current CPU core
    if (Settings::values.use_multi_core) {
        const auto& search = thread_to_cpu.find(std::this_thread::get_id());
        ASSERT(search != thread_to_cpu.end());
        ASSERT(search->second);
        return *search->second;
    }

    // Otherwise, use single-threaded mode active_core variable
    return *cpu_cores[active_core];
}

System::ResultStatus System::RunLoop(bool tight_loop) {
    status = ResultStatus::Success;

    // Update thread_to_cpu in case Core 0 is run from a different host thread
    thread_to_cpu[std::this_thread::get_id()] = cpu_cores[0];

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

System::ResultStatus System::SingleStep() {
    return RunLoop(false);
}

static FileSys::VirtualFile GetGameFileFromPath(const FileSys::VirtualFilesystem& vfs,
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

        return FileSys::ConcatenateFiles(concat, dir->GetName());
    }

    return vfs->OpenFile(path, FileSys::Mode::Read);
}

System::ResultStatus System::Load(Frontend::EmuWindow& emu_window, const std::string& filepath) {
    app_loader = Loader::GetLoader(GetGameFileFromPath(virtual_filesystem, filepath));

    if (!app_loader) {
        LOG_CRITICAL(Core, "Failed to obtain loader for {}!", filepath);
        return ResultStatus::ErrorGetLoader;
    }
    std::pair<boost::optional<u32>, Loader::ResultStatus> system_mode =
        app_loader->LoadKernelSystemMode();

    if (system_mode.second != Loader::ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to determine system mode (Error {})!",
                     static_cast<int>(system_mode.second));

        if (system_mode.second != Loader::ResultStatus::Success)
            return ResultStatus::ErrorSystemMode;
    }

    ResultStatus init_result{Init(emu_window)};
    if (init_result != ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to initialize system (Error {})!",
                     static_cast<int>(init_result));
        System::Shutdown();
        return init_result;
    }

    const Loader::ResultStatus load_result{app_loader->Load(current_process)};
    if (Loader::ResultStatus::Success != load_result) {
        LOG_CRITICAL(Core, "Failed to load ROM (Error {})!", static_cast<int>(load_result));
        System::Shutdown();

        if (load_result != Loader::ResultStatus::Success) {
            return static_cast<ResultStatus>(static_cast<u32>(ResultStatus::ErrorLoader) +
                                             static_cast<u32>(load_result));
        }
    }
    status = ResultStatus::Success;
    return status;
}

void System::PrepareReschedule() {
    CurrentCpuCore().PrepareReschedule();
}

PerfStats::Results System::GetAndResetPerfStats() {
    return perf_stats.GetAndResetStats(CoreTiming::GetGlobalTimeUs());
}

const std::shared_ptr<Kernel::Scheduler>& System::Scheduler(size_t core_index) {
    ASSERT(core_index < NUM_CPU_CORES);
    return cpu_cores[core_index]->Scheduler();
}

ARM_Interface& System::ArmInterface(size_t core_index) {
    ASSERT(core_index < NUM_CPU_CORES);
    return cpu_cores[core_index]->ArmInterface();
}

Cpu& System::CpuCore(size_t core_index) {
    ASSERT(core_index < NUM_CPU_CORES);
    return *cpu_cores[core_index];
}

System::ResultStatus System::Init(Frontend::EmuWindow& emu_window) {
    LOG_DEBUG(HW_Memory, "initialized OK");

    CoreTiming::Init();

    // Create a default fs if one doesn't already exist.
    if (virtual_filesystem == nullptr)
        virtual_filesystem = std::make_shared<FileSys::RealVfsFilesystem>();

    current_process = Kernel::Process::Create("main");

    cpu_barrier = std::make_shared<CpuBarrier>();
    cpu_exclusive_monitor = Cpu::MakeExclusiveMonitor(cpu_cores.size());
    for (size_t index = 0; index < cpu_cores.size(); ++index) {
        cpu_cores[index] = std::make_shared<Cpu>(cpu_exclusive_monitor, cpu_barrier, index);
    }

    telemetry_session = std::make_unique<Core::TelemetrySession>();
    service_manager = std::make_shared<Service::SM::ServiceManager>();

    Kernel::Init();
    Service::Init(service_manager, virtual_filesystem);
    GDBStub::Init();

    renderer = VideoCore::CreateRenderer(emu_window);
    if (!renderer->Init()) {
        return ResultStatus::ErrorVideoCore;
    }

    gpu_core = std::make_unique<Tegra::GPU>(renderer->Rasterizer());

    // Create threads for CPU cores 1-3, and build thread_to_cpu map
    // CPU core 0 is run on the main thread
    thread_to_cpu[std::this_thread::get_id()] = cpu_cores[0];
    if (Settings::values.use_multi_core) {
        for (size_t index = 0; index < cpu_core_threads.size(); ++index) {
            cpu_core_threads[index] =
                std::make_unique<std::thread>(RunCpuCore, cpu_cores[index + 1]);
            thread_to_cpu[cpu_core_threads[index]->get_id()] = cpu_cores[index + 1];
        }
    }

    LOG_DEBUG(Core, "Initialized OK");

    // Reset counters and set time origin to current frame
    GetAndResetPerfStats();
    perf_stats.BeginSystemFrame();

    return ResultStatus::Success;
}

void System::Shutdown() {
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
    Kernel::Shutdown();
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
    cpu_barrier.reset();

    // Close core timing
    CoreTiming::Shutdown();

    // Close app loader
    app_loader.reset();

    LOG_DEBUG(Core, "Shutdown OK");
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *service_manager;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *service_manager;
}

} // namespace Core
