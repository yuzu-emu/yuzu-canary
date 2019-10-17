// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/arm/exclusive_monitor.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/cpu_core_manager.h"
#include "core/gdbstub/gdbstub.h"
#include "core/settings.h"

namespace Core {
namespace {
void RunCpuCore(const System& system, Cpu& cpu_state) {
    while (system.IsPoweredOn()) {
        cpu_state.RunLoop(true);
    }
}
} // Anonymous namespace

CpuCoreManager::CpuCoreManager(System& system) : system{system} {}
CpuCoreManager::~CpuCoreManager() = default;

void CpuCoreManager::Initialize() {
    barrier = std::make_unique<CpuBarrier>();
    exclusive_monitor = Cpu::MakeExclusiveMonitor(cores.size());

    for (std::size_t index = 0; index < cores.size(); ++index) {
        cores[index] = std::make_unique<Cpu>(system, *exclusive_monitor, *barrier, index);
    }
}

void CpuCoreManager::StartThreads() {
    // Create threads for CPU cores 1-3, and build thread_to_cpu map
    // CPU core 0 is run on the main thread
    thread_to_cpu[std::this_thread::get_id()] = cores[0].get();
    if (!Settings::values.use_multi_core) {
        return;
    }

    for (std::size_t index = 0; index < core_threads.size(); ++index) {
        core_threads[index] = std::make_unique<std::thread>(RunCpuCore, std::cref(system),
                                                            std::ref(*cores[index + 1]));
        thread_to_cpu[core_threads[index]->get_id()] = cores[index + 1].get();
    }
}

void CpuCoreManager::Shutdown() {
    barrier->NotifyEnd();
    if (Settings::values.use_multi_core) {
        for (auto& thread : core_threads) {
            thread->join();
            thread.reset();
        }
    }

    thread_to_cpu.clear();
    for (auto& cpu_core : cores) {
        cpu_core->Shutdown();
        cpu_core.reset();
    }

    exclusive_monitor.reset();
    barrier.reset();
}

Cpu& CpuCoreManager::GetCore(std::size_t index) {
    return *cores.at(index);
}

const Cpu& CpuCoreManager::GetCore(std::size_t index) const {
    return *cores.at(index);
}

ExclusiveMonitor& CpuCoreManager::GetExclusiveMonitor() {
    return *exclusive_monitor;
}

const ExclusiveMonitor& CpuCoreManager::GetExclusiveMonitor() const {
    return *exclusive_monitor;
}

Cpu& CpuCoreManager::GetCurrentCore() {
    if (Settings::values.use_multi_core) {
        const auto& search = thread_to_cpu.find(std::this_thread::get_id());
        ASSERT(search != thread_to_cpu.end());
        ASSERT(search->second);
        return *search->second;
    }

    // Otherwise, use single-threaded mode active_core variable
    return *cores[active_core];
}

const Cpu& CpuCoreManager::GetCurrentCore() const {
    if (Settings::values.use_multi_core) {
        const auto& search = thread_to_cpu.find(std::this_thread::get_id());
        ASSERT(search != thread_to_cpu.end());
        ASSERT(search->second);
        return *search->second;
    }

    // Otherwise, use single-threaded mode active_core variable
    return *cores[active_core];
}

void CpuCoreManager::RunLoop(bool tight_loop) {
    // Update thread_to_cpu in case Core 0 is run from a different host thread
    thread_to_cpu[std::this_thread::get_id()] = cores[0].get();

    if (GDBStub::IsServerEnabled()) {
        GDBStub::HandlePacket();

        // If the loop is halted and we want to step, use a tiny (1) number of instructions to
        // execute. Otherwise, get out of the loop function.
        if (GDBStub::GetCpuHaltFlag()) {
            if (GDBStub::GetCpuStepFlag()) {
                tight_loop = false;
            } else {
                return;
            }
        }
    }

    auto& core_timing = system.CoreTiming();
    core_timing.ResetRun();
    bool keep_running{};
    do {
        keep_running = false;
        for (active_core = 0; active_core < NUM_CPU_CORES; ++active_core) {
            core_timing.SwitchContext(active_core);
            if (core_timing.CanCurrentContextRun()) {
                cores[active_core]->RunLoop(tight_loop);
            }
            keep_running |= core_timing.CanCurrentContextRun();
        }
    } while (keep_running);

    if (GDBStub::IsServerEnabled()) {
        GDBStub::SetCpuStepFlag(false);
    }
}

void CpuCoreManager::InvalidateAllInstructionCaches() {
    for (auto& cpu : cores) {
        cpu->ArmInterface().ClearInstructionCache();
    }
}

} // namespace Core
