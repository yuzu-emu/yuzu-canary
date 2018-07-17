// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <memory>
#include <dynarmic/A64/a64.h>
#include <dynarmic/A64/config.h>
#include "common/logging/log.h"
#include "core/arm/dynarmic/arm_dynarmic.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/memory.h"
#include "core/hle/kernel/svc.h"
#include "core/memory.h"

using Vector = Dynarmic::A64::Vector;

class ARM_Dynarmic_Callbacks : public Dynarmic::A64::UserCallbacks {
public:
    explicit ARM_Dynarmic_Callbacks(ARM_Dynarmic& parent) : parent(parent) {}
    ~ARM_Dynarmic_Callbacks() = default;

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

    void InterpreterFallback(u64 pc, size_t num_instructions) override {
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
        default:
            ASSERT_MSG(false, "ExceptionRaised(exception = {}, pc = {:X})",
                       static_cast<size_t>(exception), pc);
        }
    }

    void CallSVC(u32 swi) override {
        Kernel::CallSVC(swi);
    }

    void AddTicks(u64 ticks) override {
        CoreTiming::AddTicks(ticks - num_interpreted_instructions);
        num_interpreted_instructions = 0;
    }
    u64 GetTicksRemaining() override {
        return std::max(CoreTiming::GetDowncount(), 0);
    }
    u64 GetCNTPCT() override {
        return CoreTiming::GetTicks();
    }

    ARM_Dynarmic& parent;
    size_t num_interpreted_instructions = 0;
    u64 tpidrro_el0 = 0;
    u64 tpidr_el0 = 0;
};

std::unique_ptr<Dynarmic::A64::Jit> MakeJit(const std::unique_ptr<ARM_Dynarmic_Callbacks>& cb) {
    const auto page_table = Core::CurrentProcess()->vm_manager.page_table.pointers.data();

    Dynarmic::A64::UserConfig config;
    config.callbacks = cb.get();
    config.tpidrro_el0 = &cb->tpidrro_el0;
    config.tpidr_el0 = &cb->tpidr_el0;
    config.dczid_el0 = 4;
    config.ctr_el0 = 0x8444c004;
    config.page_table = reinterpret_cast<void**>(page_table);
    config.page_table_address_space_bits = Memory::ADDRESS_SPACE_BITS;
    config.silently_mirror_page_table = false;

    return std::make_unique<Dynarmic::A64::Jit>(config);
}

void ARM_Dynarmic::Run() {
    ASSERT(Memory::GetCurrentPageTable() == current_page_table);

    jit->Run();
}

void ARM_Dynarmic::Step() {
    cb->InterpreterFallback(jit->GetPC(), 1);
}

ARM_Dynarmic::ARM_Dynarmic()
    : cb(std::make_unique<ARM_Dynarmic_Callbacks>(*this)), jit(MakeJit(cb)) {
    ARM_Interface::ThreadContext ctx;
    inner_unicorn.SaveContext(ctx);
    LoadContext(ctx);
    PageTableChanged();
}

ARM_Dynarmic::~ARM_Dynarmic() = default;

void ARM_Dynarmic::MapBackingMemory(u64 address, size_t size, u8* memory,
                                    Kernel::VMAPermission perms) {
    inner_unicorn.MapBackingMemory(address, size, memory, perms);
}

void ARM_Dynarmic::UnmapMemory(u64 address, size_t size) {
    inner_unicorn.UnmapMemory(address, size);
}

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

u128 ARM_Dynarmic::GetExtReg(int index) const {
    return jit->GetVector(index);
}

void ARM_Dynarmic::SetExtReg(int index, u128 value) {
    jit->SetVector(index, value);
}

u32 ARM_Dynarmic::GetVFPReg(int /*index*/) const {
    UNIMPLEMENTED();
    return {};
}

void ARM_Dynarmic::SetVFPReg(int /*index*/, u32 /*value*/) {
    UNIMPLEMENTED();
}

u32 ARM_Dynarmic::GetCPSR() const {
    return jit->GetPstate();
}

void ARM_Dynarmic::SetCPSR(u32 cpsr) {
    jit->SetPstate(cpsr);
}

u64 ARM_Dynarmic::GetTlsAddress() const {
    return cb->tpidrro_el0;
}

void ARM_Dynarmic::SetTlsAddress(u64 address) {
    cb->tpidrro_el0 = address;
}

void ARM_Dynarmic::SaveContext(ARM_Interface::ThreadContext& ctx) {
    ctx.cpu_registers = jit->GetRegisters();
    ctx.sp = jit->GetSP();
    ctx.pc = jit->GetPC();
    ctx.cpsr = jit->GetPstate();
    ctx.fpu_registers = jit->GetVectors();
    ctx.fpscr = jit->GetFpcr();
    ctx.tls_address = cb->tpidrro_el0;
}

void ARM_Dynarmic::LoadContext(const ARM_Interface::ThreadContext& ctx) {
    jit->SetRegisters(ctx.cpu_registers);
    jit->SetSP(ctx.sp);
    jit->SetPC(ctx.pc);
    jit->SetPstate(static_cast<u32>(ctx.cpsr));
    jit->SetVectors(ctx.fpu_registers);
    jit->SetFpcr(static_cast<u32>(ctx.fpscr));
    cb->tpidrro_el0 = ctx.tls_address;
}

void ARM_Dynarmic::PrepareReschedule() {
    if (jit->IsExecuting()) {
        jit->HaltExecution();
    }
}

void ARM_Dynarmic::ClearInstructionCache() {
    jit->ClearCache();
}

void ARM_Dynarmic::ClearExclusiveState() {
    jit->ClearExclusiveState();
}

void ARM_Dynarmic::PageTableChanged() {
    jit = MakeJit(cb);
    current_page_table = Memory::GetCurrentPageTable();
}
