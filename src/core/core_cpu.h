// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include "common/common_types.h"

namespace Kernel {
class GlobalScheduler;
class Scheduler;
} // namespace Kernel

namespace Core {
class System;
}

namespace Core::Timing {
class CoreTiming;
}

namespace Core {

class ARM_Interface;
class ExclusiveMonitor;

constexpr unsigned NUM_CPU_CORES{4};

class CpuBarrier {
public:
    bool IsAlive() const {
        return !end;
    }

    void NotifyEnd();

    bool Rendezvous();

private:
    unsigned cores_waiting{NUM_CPU_CORES};
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> end{};
};

class Cpu {
public:
    Cpu(System& system, ExclusiveMonitor& exclusive_monitor, CpuBarrier& cpu_barrier,
        std::size_t core_index);
    ~Cpu();

    void RunLoop(bool tight_loop = true);

    void SingleStep();

    void PrepareReschedule();

    ARM_Interface& ArmInterface() {
        return *arm_interface;
    }

    const ARM_Interface& ArmInterface() const {
        return *arm_interface;
    }

    Kernel::Scheduler& Scheduler() {
        return *scheduler;
    }

    const Kernel::Scheduler& Scheduler() const {
        return *scheduler;
    }

    bool IsMainCore() const {
        return core_index == 0;
    }

    std::size_t CoreIndex() const {
        return core_index;
    }

    static std::unique_ptr<ExclusiveMonitor> MakeExclusiveMonitor(std::size_t num_cores);

private:
    void Reschedule();

    std::unique_ptr<ARM_Interface> arm_interface;
    CpuBarrier& cpu_barrier;
    Kernel::GlobalScheduler& global_scheduler;
    std::unique_ptr<Kernel::Scheduler> scheduler;
    Timing::CoreTiming& core_timing;

    std::atomic<bool> reschedule_pending = false;
    std::size_t core_index;
};

} // namespace Core
