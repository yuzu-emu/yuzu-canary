// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unicorn/unicorn.h>
#include "common/common_types.h"
#include "core/arm/arm_interface.h"
#include "core/gdbstub/gdbstub.h"

namespace Core::Timing {
class CoreTiming;
}

namespace Core {

class ARM_Unicorn final : public ARM_Interface {
public:
    explicit ARM_Unicorn(Timing::CoreTiming& core_timing);
    ~ARM_Unicorn();

    void MapBackingMemory(VAddr address, std::size_t size, u8* memory,
                          Kernel::VMAPermission perms) override;
    void UnmapMemory(VAddr address, std::size_t size) override;
    void SetPC(u64 pc) override;
    u64 GetPC() const override;
    u64 GetReg(int index) const override;
    void SetReg(int index, u64 value) override;
    u128 GetVectorReg(int index) const override;
    void SetVectorReg(int index, u128 value) override;
    u32 GetPSTATE() const override;
    void SetPSTATE(u32 pstate) override;
    VAddr GetTlsAddress() const override;
    void SetTlsAddress(VAddr address) override;
    void SetTPIDR_EL0(u64 value) override;
    u64 GetTPIDR_EL0() const override;
    void SaveContext(ThreadContext& ctx) override;
    void LoadContext(const ThreadContext& ctx) override;
    void PrepareReschedule() override;
    void ClearExclusiveState() override;
    void ExecuteInstructions(int num_instructions);
    void Run() override;
    void Step() override;
    void ClearInstructionCache() override;
    void PageTableChanged() override{};
    void RecordBreak(GDBStub::BreakpointAddress bkpt);

private:
    uc_engine* uc{};
    Timing::CoreTiming& core_timing;
    GDBStub::BreakpointAddress last_bkpt{};
    bool last_bkpt_hit;
};

} // namespace Core
