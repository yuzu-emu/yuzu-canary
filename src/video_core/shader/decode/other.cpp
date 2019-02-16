// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

using Tegra::Shader::ConditionCode;
using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Register;

u32 ShaderIR::DecodeOther(NodeBlock& bb, u32 pc) {
    const Instruction instr = {program_code[pc]};
    const auto opcode = OpCode::Decode(instr);

    switch (opcode->get().GetId()) {
    case OpCode::Id::EXIT: {
        const Tegra::Shader::ConditionCode cc = instr.flow_condition_code;
        UNIMPLEMENTED_IF_MSG(cc != Tegra::Shader::ConditionCode::T, "EXIT condition code used: {}",
                             static_cast<u32>(cc));

        switch (instr.flow.cond) {
        case Tegra::Shader::FlowCondition::Always:
            bb.push_back(Operation(OperationCode::Exit));
            if (instr.pred.pred_index == static_cast<u64>(Tegra::Shader::Pred::UnusedIndex)) {
                // If this is an unconditional exit then just end processing here,
                // otherwise we have to account for the possibility of the condition
                // not being met, so continue processing the next instruction.
                pc = MAX_PROGRAM_LENGTH - 1;
            }
            break;

        case Tegra::Shader::FlowCondition::Fcsm_Tr:
            // TODO(bunnei): What is this used for? If we assume this conditon is not
            // satisifed, dual vertex shaders in Farming Simulator make more sense
            UNIMPLEMENTED_MSG("Skipping unknown FlowCondition::Fcsm_Tr");
            break;

        default:
            UNIMPLEMENTED_MSG("Unhandled flow condition: {}",
                              static_cast<u32>(instr.flow.cond.Value()));
        }
        break;
    }
    case OpCode::Id::KIL: {
        UNIMPLEMENTED_IF(instr.flow.cond != Tegra::Shader::FlowCondition::Always);

        const Tegra::Shader::ConditionCode cc = instr.flow_condition_code;
        UNIMPLEMENTED_IF_MSG(cc != Tegra::Shader::ConditionCode::T, "KIL condition code used: {}",
                             static_cast<u32>(cc));

        bb.push_back(Operation(OperationCode::Discard));
        break;
    }
    case OpCode::Id::MOV_SYS: {
        switch (instr.sys20) {
        case Tegra::Shader::SystemVariable::InvocationInfo: {
            LOG_WARNING(HW_GPU, "MOV_SYS instruction with InvocationInfo is incomplete");
            SetRegister(bb, instr.gpr0, Immediate(0u));
            break;
        }
        case Tegra::Shader::SystemVariable::Ydirection: {
            // Config pack's third value is Y_NEGATE's state.
            SetRegister(bb, instr.gpr0, Operation(OperationCode::YNegate));
            break;
        }
        default:
            UNIMPLEMENTED_MSG("Unhandled system move: {}", static_cast<u32>(instr.sys20.Value()));
        }
        break;
    }
    case OpCode::Id::BRA: {
        UNIMPLEMENTED_IF_MSG(instr.bra.constant_buffer != 0,
                             "BRA with constant buffers are not implemented");

        const u32 target = pc + instr.bra.GetBranchTarget();
        const Node branch = Operation(OperationCode::Branch, Immediate(target));

        const Tegra::Shader::ConditionCode cc = instr.flow_condition_code;
        if (cc != Tegra::Shader::ConditionCode::T) {
            bb.push_back(Conditional(GetConditionCode(cc), {branch}));
        } else {
            bb.push_back(branch);
        }
        break;
    }
    case OpCode::Id::SSY: {
        UNIMPLEMENTED_IF_MSG(instr.bra.constant_buffer != 0,
                             "Constant buffer flow is not supported");

        // The SSY opcode tells the GPU where to re-converge divergent execution paths, it sets the
        // target of the jump that the SYNC instruction will make. The SSY opcode has a similar
        // structure to the BRA opcode.
        const u32 target = pc + instr.bra.GetBranchTarget();
        bb.push_back(Operation(OperationCode::PushFlowStack, Immediate(target)));
        break;
    }
    case OpCode::Id::PBK: {
        UNIMPLEMENTED_IF_MSG(instr.bra.constant_buffer != 0,
                             "Constant buffer PBK is not supported");

        // PBK pushes to a stack the address where BRK will jump to. This shares stack with SSY but
        // using SYNC on a PBK address will kill the shader execution. We don't emulate this because
        // it's very unlikely a driver will emit such invalid shader.
        const u32 target = pc + instr.bra.GetBranchTarget();
        bb.push_back(Operation(OperationCode::PushFlowStack, Immediate(target)));
        break;
    }
    case OpCode::Id::SYNC: {
        const Tegra::Shader::ConditionCode cc = instr.flow_condition_code;
        UNIMPLEMENTED_IF_MSG(cc != Tegra::Shader::ConditionCode::T, "SYNC condition code used: {}",
                             static_cast<u32>(cc));

        // The SYNC opcode jumps to the address previously set by the SSY opcode
        bb.push_back(Operation(OperationCode::PopFlowStack));
        break;
    }
    case OpCode::Id::BRK: {
        const Tegra::Shader::ConditionCode cc = instr.flow_condition_code;
        UNIMPLEMENTED_IF_MSG(cc != Tegra::Shader::ConditionCode::T, "BRK condition code used: {}",
                             static_cast<u32>(cc));

        // The BRK opcode jumps to the address previously set by the PBK opcode
        bb.push_back(Operation(OperationCode::PopFlowStack));
        break;
    }
    case OpCode::Id::IPA: {
        const auto& attribute = instr.attribute.fmt28;
        const Tegra::Shader::IpaMode input_mode{instr.ipa.interp_mode.Value(),
                                                instr.ipa.sample_mode.Value()};

        const Node attr = GetInputAttribute(attribute.index, attribute.element, input_mode);
        Node value = attr;
        const Tegra::Shader::Attribute::Index index = attribute.index.Value();
        if (index >= Tegra::Shader::Attribute::Index::Attribute_0 &&
            index <= Tegra::Shader::Attribute::Index::Attribute_31) {
            // TODO(Blinkhawk): There are cases where a perspective attribute use PASS.
            // In theory by setting them as perspective, OpenGL does the perspective correction.
            // A way must figured to reverse the last step of it.
            if (input_mode.interpolation_mode == Tegra::Shader::IpaInterpMode::Multiply) {
                value = Operation(OperationCode::FMul, PRECISE, value, GetRegister(instr.gpr20));
            }
        }
        value = GetSaturatedFloat(value, instr.ipa.saturate);

        SetRegister(bb, instr.gpr0, value);
        break;
    }
    case OpCode::Id::OUT_R: {
        UNIMPLEMENTED_IF_MSG(instr.gpr20.Value() != Register::ZeroIndex,
                             "Stream buffer is not supported");

        if (instr.out.emit) {
            // gpr0 is used to store the next address and gpr8 contains the address to emit.
            // Hardware uses pointers here but we just ignore it
            bb.push_back(Operation(OperationCode::EmitVertex));
            SetRegister(bb, instr.gpr0, Immediate(0));
        }
        if (instr.out.cut) {
            bb.push_back(Operation(OperationCode::EndPrimitive));
        }
        break;
    }
    case OpCode::Id::ISBERD: {
        UNIMPLEMENTED_IF(instr.isberd.o != 0);
        UNIMPLEMENTED_IF(instr.isberd.skew != 0);
        UNIMPLEMENTED_IF(instr.isberd.shift != Tegra::Shader::IsberdShift::None);
        UNIMPLEMENTED_IF(instr.isberd.mode != Tegra::Shader::IsberdMode::None);
        LOG_WARNING(HW_GPU, "ISBERD instruction is incomplete");
        SetRegister(bb, instr.gpr0, GetRegister(instr.gpr8));
        break;
    }
    case OpCode::Id::DEPBAR: {
        LOG_WARNING(HW_GPU, "DEPBAR instruction is stubbed");
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unhandled instruction: {}", opcode->get().GetName());
    }

    return pc;
}

} // namespace VideoCommon::Shader
