// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <memory>
#include <dynarmic/A64/a64.h>
#include <dynarmic/A64/config.h>
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "core/arm/dynarmic/arm_dynarmic.h"
#include "core/core.h"
#include "core/core_cpu.h"
#include "core/core_timing.h"
#include "core/core_timing_util.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/vm_manager.h"
#include "core/memory.h"

namespace Core {

using Vector = Dynarmic::A64::Vector;

class ARM_Dynarmic_Callbacks : public Dynarmic::A64::UserCallbacks {
public:
    explicit ARM_Dynarmic_Callbacks(ARM_Dynarmic& parent) : parent(parent) {}

    u8 MemoryRead8(u64 vaddr) override {
        return Memory::Read8(vaddr);
    }
    u16 MemoryRead16(u64 vaddr) override {
        return Memory::Read16(vaddr);
    }
    u32 MemoryRead32(u64 vaddr) override {
        return Memory::Read32(vaddr);
    }
    u64 MemoryRead64(u64 vaddr) override {
        return Memory::Read64(vaddr);
    }
    Vector MemoryRead128(u64 vaddr) override {
        return {Memory::Read64(vaddr), Memory::Read64(vaddr + 8)};
    }

    void MemoryWrite8(u64 vaddr, u8 value) override {
        Memory::Write8(vaddr, value);
    }
    void MemoryWrite16(u64 vaddr, u16 value) override {
        Memory::Write16(vaddr, value);
    }
    void MemoryWrite32(u64 vaddr, u32 value) override {
        Memory::Write32(vaddr, value);
    }
    void MemoryWrite64(u64 vaddr, u64 value) override {
        Memory::Write64(vaddr, value);
    }
    void MemoryWrite128(u64 vaddr, Vector value) override {
        Memory::Write64(vaddr, value[0]);
        Memory::Write64(vaddr + 8, value[1]);
    }

    void InterpreterFallback(u64 pc, std::size_t num_instructions) override {
        LOG_INFO(Core_ARM, "Unicorn fallback @ 0x{:X} for {} instructions (instr = {:08X})", pc,
                 num_instructions, MemoryReadCode(pc));

        ARM_Interface::ThreadContext ctx;
        parent.SaveContext(ctx);
        parent.inner_unicorn.LoadContext(ctx);
        parent.inner_unicorn.ExecuteInstructions(static_cast<int>(num_instructions));
        parent.inner_unicorn.SaveContext(ctx);
        parent.LoadContext(ctx);
        num_interpreted_instructions += num_instructions;
    }

    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception exception) override {
        switch (exception) {
        case Dynarmic::A64::Exception::WaitForInterrupt:
        case Dynarmic::A64::Exception::WaitForEvent:
        case Dynarmic::A64::Exception::SendEvent:
        case Dynarmic::A64::Exception::SendEventLocal:
        case Dynarmic::A64::Exception::Yield:
            return;
        case Dynarmic::A64::Exception::Breakpoint:
            if (GDBStub::IsServerEnabled()) {
                parent.jit->HaltExecution();
                parent.SetPC(pc);
                Kernel::Thread* thread = Kernel::GetCurrentThread();
                parent.SaveContext(thread->GetContext());
                GDBStub::Break();
                GDBStub::SendTrap(thread, 5);
                return;
            }
            [[fallthrough]];
        default:
            ASSERT_MSG(false, "ExceptionRaised(exception = {}, pc = {:X})",
                       static_cast<std::size_t>(exception), pc);
        }
    }

    void CallSVC(u32 swi) override {
        Kernel::CallSVC(parent.system, swi);
    }

    void AddTicks(u64 ticks) override {
        // Divide the number of ticks by the amount of CPU cores. TODO(Subv): This yields only a
        // rough approximation of the amount of executed ticks in the system, it may be thrown off
        // if not all cores are doing a similar amount of work. Instead of doing this, we should
        // device a way so that timing is consistent across all cores without increasing the ticks 4
        // times.
        u64 amortized_ticks = (ticks - num_interpreted_instructions) / Core::NUM_CPU_CORES;
        // Always execute at least one tick.
        amortized_ticks = std::max<u64>(amortized_ticks, 1);

        parent.system.CoreTiming().AddTicks(amortized_ticks);
        num_interpreted_instructions = 0;
    }
    u64 GetTicksRemaining() override {
        return std::max(parent.system.CoreTiming().GetDowncount(), 0);
    }
    u64 GetCNTPCT() override {
        return Timing::CpuCyclesToClockCycles(parent.system.CoreTiming().GetTicks());
    }

    ARM_Dynarmic& parent;
    std::size_t num_interpreted_instructions = 0;
    u64 tpidrro_el0 = 0;
    u64 tpidr_el0 = 0;
};

std::unique_ptr<Dynarmic::A64::Jit> ARM_Dynarmic::MakeJit(Common::PageTable& page_table,
                                                          std::size_t address_space_bits) const {
    Dynarmic::A64::UserConfig config;

    // Callbacks
    config.callbacks = cb.get();

    // Memory
    config.page_table = reinterpret_cast<void**>(page_table.pointers.data());
    config.page_table_address_space_bits = address_space_bits;
    config.silently_mirror_page_table = false;

    // Multi-process state
    config.processor_id = core_index;
    config.global_monitor = &exclusive_monitor.monitor;

    // System registers
    config.tpidrro_el0 = &cb->tpidrro_el0;
    config.tpidr_el0 = &cb->tpidr_el0;
    config.dczid_el0 = 4;
    config.ctr_el0 = 0x8444c004;
    config.cntfrq_el0 = Timing::CNTFREQ;

    // Unpredictable instructions
    config.define_unpredictable_behaviour = true;

    return std::make_unique<Dynarmic::A64::Jit>(config);
}

MICROPROFILE_DEFINE(ARM_Jit_Dynarmic, "ARM JIT", "Dynarmic", MP_RGB(255, 64, 64));

void ARM_Dynarmic::Run() {
    MICROPROFILE_SCOPE(ARM_Jit_Dynarmic);

    jit->Run();
}

void ARM_Dynarmic::Step() {
    cb->InterpreterFallback(jit->GetPC(), 1);
}

ARM_Dynarmic::ARM_Dynarmic(System& system, ExclusiveMonitor& exclusive_monitor,
                           std::size_t core_index)
    : cb(std::make_unique<ARM_Dynarmic_Callbacks>(*this)), inner_unicorn{system},
      core_index{core_index}, system{system},
      exclusive_monitor{dynamic_cast<DynarmicExclusiveMonitor&>(exclusive_monitor)} {}

ARM_Dynarmic::~ARM_Dynarmic() = default;

void ARM_Dynarmic::SetPC(u64 pc) {
    jit->SetPC(pc);
}

u64 ARM_Dynarmic::GetPC() const {
    return jit->GetPC();
}

u64 ARM_Dynarmic::GetReg(int index) const {
    return jit->GetRegister(index);
}

void ARM_Dynarmic::SetReg(int index, u64 value) {
    jit->SetRegister(index, value);
}

u128 ARM_Dynarmic::GetVectorReg(int index) const {
    return jit->GetVector(index);
}

void ARM_Dynarmic::SetVectorReg(int index, u128 value) {
    jit->SetVector(index, value);
}

u32 ARM_Dynarmic::GetPSTATE() const {
    return jit->GetPstate();
}

void ARM_Dynarmic::SetPSTATE(u32 pstate) {
    jit->SetPstate(pstate);
}

u64 ARM_Dynarmic::GetTlsAddress() const {
    return cb->tpidrro_el0;
}

void ARM_Dynarmic::SetTlsAddress(VAddr address) {
    cb->tpidrro_el0 = address;
}

u64 ARM_Dynarmic::GetTPIDR_EL0() const {
    return cb->tpidr_el0;
}

void ARM_Dynarmic::SetTPIDR_EL0(u64 value) {
    cb->tpidr_el0 = value;
}

void ARM_Dynarmic::SaveContext(ThreadContext& ctx) {
    ctx.cpu_registers = jit->GetRegisters();
    ctx.sp = jit->GetSP();
    ctx.pc = jit->GetPC();
    ctx.pstate = jit->GetPstate();
    ctx.vector_registers = jit->GetVectors();
    ctx.fpcr = jit->GetFpcr();
    ctx.fpsr = jit->GetFpsr();
    ctx.tpidr = cb->tpidr_el0;
}

void ARM_Dynarmic::LoadContext(const ThreadContext& ctx) {
    jit->SetRegisters(ctx.cpu_registers);
    jit->SetSP(ctx.sp);
    jit->SetPC(ctx.pc);
    jit->SetPstate(ctx.pstate);
    jit->SetVectors(ctx.vector_registers);
    jit->SetFpcr(ctx.fpcr);
    jit->SetFpsr(ctx.fpsr);
    SetTPIDR_EL0(ctx.tpidr);
}

void ARM_Dynarmic::PrepareReschedule() {
    if (jit == nullptr)
        return;

    jit->HaltExecution();
}

void ARM_Dynarmic::ClearInstructionCache() {
    jit->ClearCache();
}

void ARM_Dynarmic::ClearExclusiveState() {
    jit->ClearExclusiveState();
}

void ARM_Dynarmic::PageTableChanged(Common::PageTable& page_table,
                                    std::size_t new_address_space_size_in_bits) {
    jit = MakeJit(page_table, new_address_space_size_in_bits);
}

DynarmicExclusiveMonitor::DynarmicExclusiveMonitor(std::size_t core_count) : monitor(core_count) {}
DynarmicExclusiveMonitor::~DynarmicExclusiveMonitor() = default;

void DynarmicExclusiveMonitor::SetExclusive(std::size_t core_index, VAddr addr) {
    // Size doesn't actually matter.
    monitor.Mark(core_index, addr, 16);
}

void DynarmicExclusiveMonitor::ClearExclusive() {
    monitor.Clear();
}

bool DynarmicExclusiveMonitor::ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) {
    return monitor.DoExclusiveOperation(core_index, vaddr, 1,
                                        [&] { Memory::Write8(vaddr, value); });
}

bool DynarmicExclusiveMonitor::ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) {
    return monitor.DoExclusiveOperation(core_index, vaddr, 2,
                                        [&] { Memory::Write16(vaddr, value); });
}

bool DynarmicExclusiveMonitor::ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) {
    return monitor.DoExclusiveOperation(core_index, vaddr, 4,
                                        [&] { Memory::Write32(vaddr, value); });
}

bool DynarmicExclusiveMonitor::ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) {
    return monitor.DoExclusiveOperation(core_index, vaddr, 8,
                                        [&] { Memory::Write64(vaddr, value); });
}

bool DynarmicExclusiveMonitor::ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) {
    return monitor.DoExclusiveOperation(core_index, vaddr, 16, [&] {
        Memory::Write64(vaddr + 0, value[0]);
        Memory::Write64(vaddr + 8, value[1]);
    });
}

} // namespace Core
