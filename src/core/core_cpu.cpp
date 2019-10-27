// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <condition_variable>
#include <mutex>

#include "common/logging/log.h"
#ifdef ARCHITECTURE_x86_64
#include "core/arm/dynarmic/arm_dynarmic.h"
#endif
#include "core/arm/exclusive_monitor.h"
#include "core/arm/unicorn/arm_unicorn.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/lock.h"
#include "core/settings.h"

namespace Core {

void CpuBarrier::NotifyEnd() {
    std::unique_lock lock{mutex};
    end = true;
    condition.notify_all();
}

bool CpuBarrier::Rendezvous() {
    if (!Settings::values.use_multi_core) {
        // Meaningless when running in single-core mode
        return true;
    }

    if (!end) {
        std::unique_lock lock{mutex};

        --cores_waiting;
        if (!cores_waiting) {
            cores_waiting = NUM_CPU_CORES;
            condition.notify_all();
            return true;
        }

        condition.wait(lock);
        return true;
    }

    return false;
}

Cpu::Cpu(System& system, ExclusiveMonitor& exclusive_monitor, CpuBarrier& cpu_barrier,
         std::size_t core_index)
    : cpu_barrier{cpu_barrier}, global_scheduler{system.GlobalScheduler()},
      core_timing{system.CoreTiming()}, core_index{core_index} {
#ifdef ARCHITECTURE_x86_64
    arm_interface = std::make_unique<ARM_Dynarmic>(system, exclusive_monitor, core_index);
#else
    arm_interface = std::make_unique<ARM_Unicorn>(system);
    LOG_WARNING(Core, "CPU JIT requested, but Dynarmic not available");
#endif

    scheduler = std::make_unique<Kernel::Scheduler>(system, *arm_interface, core_index);
}

Cpu::~Cpu() = default;

std::unique_ptr<ExclusiveMonitor> Cpu::MakeExclusiveMonitor(std::size_t num_cores) {
#ifdef ARCHITECTURE_x86_64
    return std::make_unique<DynarmicExclusiveMonitor>(num_cores);
#else
    // TODO(merry): Passthrough exclusive monitor
    return nullptr;
#endif
}

void Cpu::RunLoop(bool tight_loop) {
    // Wait for all other CPU cores to complete the previous slice, such that they run in lock-step
    if (!cpu_barrier.Rendezvous()) {
        // If rendezvous failed, session has been killed
        return;
    }

    Reschedule();

    // If we don't have a currently active thread then don't execute instructions,
    // instead advance to the next event and try to yield to the next thread
    if (Kernel::GetCurrentThread() == nullptr) {
        LOG_TRACE(Core, "Core-{} idling", core_index);
        core_timing.Idle();
    } else {
        if (tight_loop) {
            arm_interface->Run();
        } else {
            arm_interface->Step();
        }
    }
    core_timing.Advance();

    Reschedule();
}

void Cpu::SingleStep() {
    return RunLoop(false);
}

void Cpu::PrepareReschedule() {
    arm_interface->PrepareReschedule();
}

void Cpu::Reschedule() {
    // Lock the global kernel mutex when we manipulate the HLE state
    std::lock_guard lock(HLE::g_hle_lock);

    global_scheduler.SelectThread(core_index);
    scheduler->TryDoContextSwitch();
}

void Cpu::Shutdown() {
    scheduler->Shutdown();
}

} // namespace Core
