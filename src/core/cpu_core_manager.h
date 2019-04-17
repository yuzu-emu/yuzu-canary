// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <map>
#include <memory>
#include <thread>

namespace Core {

class Cpu;
class CpuBarrier;
class ExclusiveMonitor;
class System;

class CpuCoreManager {
public:
    explicit CpuCoreManager(System& system);
    CpuCoreManager(const CpuCoreManager&) = delete;
    CpuCoreManager(CpuCoreManager&&) = delete;

    ~CpuCoreManager();

    CpuCoreManager& operator=(const CpuCoreManager&) = delete;
    CpuCoreManager& operator=(CpuCoreManager&&) = delete;

    void Initialize();
    void StartThreads();
    void Shutdown();

    Cpu& GetCore(std::size_t index);
    const Cpu& GetCore(std::size_t index) const;

    Cpu& GetCurrentCore();
    const Cpu& GetCurrentCore() const;

    ExclusiveMonitor& GetExclusiveMonitor();
    const ExclusiveMonitor& GetExclusiveMonitor() const;

    void RunLoop(bool tight_loop);

    void InvalidateAllInstructionCaches();

private:
    static constexpr std::size_t NUM_CPU_CORES = 4;

    std::unique_ptr<ExclusiveMonitor> exclusive_monitor;
    std::unique_ptr<CpuBarrier> barrier;
    std::array<std::unique_ptr<Cpu>, NUM_CPU_CORES> cores;
    std::array<std::unique_ptr<std::thread>, NUM_CPU_CORES - 1> core_threads;
    std::size_t active_core{}; ///< Active core, only used in single thread mode

    /// Map of guest threads to CPU cores
    std::map<std::thread::id, Cpu*> thread_to_cpu;

    System& system;
};

} // namespace Core
