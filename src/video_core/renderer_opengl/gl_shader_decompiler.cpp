// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <set>
#include <string>
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"

namespace GLShader {
namespace Decompiler {

using Tegra::Shader::Attribute;
using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;
using Tegra::Shader::Register;
using Tegra::Shader::Sampler;
using Tegra::Shader::SubOp;
using Tegra::Shader::Uniform;

constexpr u32 PROGRAM_END = MAX_PROGRAM_CODE_LENGTH;

class DecompileFail : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Describes the behaviour of code path of a given entry point and a return point.
enum class ExitMethod {
    Undetermined, ///< Internal value. Only occur when analyzing JMP loop.
    AlwaysReturn, ///< All code paths reach the return point.
    Conditional,  ///< Code path reaches the return point or an END instruction conditionally.
    AlwaysEnd,    ///< All code paths reach a END instruction.
};

/// A subroutine is a range of code refereced by a CALL, IF or LOOP instruction.
struct Subroutine {
    /// Generates a name suitable for GLSL source code.
    std::string GetName() const {
        return "sub_" + std::to_string(begin) + "_" + std::to_string(end);
    }

    u32 begin;              ///< Entry point of the subroutine.
    u32 end;                ///< Return point of the subroutine.
    ExitMethod exit_method; ///< Exit method of the subroutine.
    std::set<u32> labels;   ///< Addresses refereced by JMP instructions.

    bool operator<(const Subroutine& rhs) const {
        return std::tie(begin, end) < std::tie(rhs.begin, rhs.end);
    }
};

/// Analyzes shader code and produces a set of subroutines.
class ControlFlowAnalyzer {
public:
    ControlFlowAnalyzer(const ProgramCode& program_code, u32 main_offset)
        : program_code(program_code) {

        // Recursively finds all subroutines.
        const Subroutine& program_main = AddSubroutine(main_offset, PROGRAM_END);
        if (program_main.exit_method != ExitMethod::AlwaysEnd)
            throw DecompileFail("Program does not always end");
    }

    std::set<Subroutine> GetSubroutines() {
        return std::move(subroutines);
    }

private:
    const ProgramCode& program_code;
    std::set<Subroutine> subroutines;
    std::map<std::pair<u32, u32>, ExitMethod> exit_method_map;

    /// Adds and analyzes a new subroutine if it is not added yet.
    const Subroutine& AddSubroutine(u32 begin, u32 end) {
        auto iter = subroutines.find(Subroutine{begin, end});
        if (iter != subroutines.end())
            return *iter;

        Subroutine subroutine{begin, end};
        subroutine.exit_method = Scan(begin, end, subroutine.labels);
        if (subroutine.exit_method == ExitMethod::Undetermined)
            throw DecompileFail("Recursive function detected");
        return *subroutines.insert(std::move(subroutine)).first;
    }

    /// Scans a range of code for labels and determines the exit method.
    ExitMethod Scan(u32 begin, u32 end, std::set<u32>& labels) {
        auto [iter, inserted] =
            exit_method_map.emplace(std::make_pair(begin, end), ExitMethod::Undetermined);
        ExitMethod& exit_method = iter->second;
        if (!inserted)
            return exit_method;

        for (u32 offset = begin; offset != end && offset != PROGRAM_END; ++offset) {
            const Instruction instr = {program_code[offset]};
            switch (instr.opcode.EffectiveOpCode()) {
            case OpCode::Id::EXIT: {
                return exit_method = ExitMethod::AlwaysEnd;
            }
            }
        }
        return exit_method = ExitMethod::AlwaysReturn;
    }
};

class ShaderWriter {
public:
    void AddLine(const std::string& text) {
        DEBUG_ASSERT(scope >= 0);
        if (!text.empty()) {
            shader_source += std::string(static_cast<size_t>(scope) * 4, ' ');
        }
        shader_source += text + '\n';
    }

    std::string GetResult() {
        return std::move(shader_source);
    }

    int scope = 0;

private:
    std::string shader_source;
};

class GLSLGenerator {
public:
    GLSLGenerator(const std::set<Subroutine>& subroutines, const ProgramCode& program_code,
                  u32 main_offset, Maxwell3D::Regs::ShaderStage stage)
        : subroutines(subroutines), program_code(program_code), main_offset(main_offset),
          stage(stage) {

        Generate();
    }

    std::string GetShaderCode() {
        return declarations.GetResult() + shader.GetResult();
    }

    /// Returns entries in the shader that are useful for external functions
    ShaderEntries GetEntries() const {
        return {GetConstBuffersDeclarations()};
    }

private:
    /// Gets the Subroutine object corresponding to the specified address.
    const Subroutine& GetSubroutine(u32 begin, u32 end) const {
        auto iter = subroutines.find(Subroutine{begin, end});
        ASSERT(iter != subroutines.end());
        return *iter;
    }

    /// Generates code representing an input attribute register.
    std::string GetInputAttribute(Attribute::Index attribute) {
        switch (attribute) {
        case Attribute::Index::Position:
            return "position";
        default:
            const u32 index{static_cast<u32>(attribute) -
                            static_cast<u32>(Attribute::Index::Attribute_0)};
            if (attribute >= Attribute::Index::Attribute_0) {
                declr_input_attribute.insert(attribute);
                return "input_attribute_" + std::to_string(index);
            }

            LOG_CRITICAL(HW_GPU, "Unhandled input attribute: 0x%02x", index);
            UNREACHABLE();
        }
    }

    /// Generates code representing an output attribute register.
    std::string GetOutputAttribute(Attribute::Index attribute) {
        switch (attribute) {
        case Attribute::Index::Position:
            return "position";
        default:
            const u32 index{static_cast<u32>(attribute) -
                            static_cast<u32>(Attribute::Index::Attribute_0)};
            if (attribute >= Attribute::Index::Attribute_0) {
                declr_output_attribute.insert(attribute);
                return "output_attribute_" + std::to_string(index);
            }

            LOG_CRITICAL(HW_GPU, "Unhandled output attribute: 0x%02x", index);
            UNREACHABLE();
        }
    }

    /// Generates code representing an immediate value
    static std::string GetImmediate(const Instruction& instr) {
        return std::to_string(instr.alu.GetImm20());
    }

    /// Generates code representing a temporary (GPR) register.
    std::string GetRegister(const Register& reg, unsigned elem = 0) {
        if (stage == Maxwell3D::Regs::ShaderStage::Fragment && reg < 4) {
            // GPRs 0-3 are output color for the fragment shader
            return std::string{"color."} + "rgba"[(reg + elem) & 3];
        }

        return *declr_register.insert("register_" + std::to_string(reg + elem)).first;
    }

    /// Generates code representing a uniform (C buffer) register.
    std::string GetUniform(const Uniform& reg) {
        declr_const_buffers[reg.index].MarkAsUsed(reg.index, reg.offset, stage);
        return 'c' + std::to_string(reg.index) + '[' + std::to_string(reg.offset) + ']';
    }

    /// Generates code representing a texture sampler.
    std::string GetSampler(const Sampler& sampler) const {
        // TODO(Subv): Support more than just texture sampler 0
        ASSERT_MSG(sampler.index == Sampler::Index::Sampler_0, "unsupported");
        const unsigned index{static_cast<unsigned>(sampler.index.Value()) -
                             static_cast<unsigned>(Sampler::Index::Sampler_0)};
        return "tex[" + std::to_string(index) + "]";
    }

    /**
     * Adds code that calls a subroutine.
     * @param subroutine the subroutine to call.
     */
    void CallSubroutine(const Subroutine& subroutine) {
        if (subroutine.exit_method == ExitMethod::AlwaysEnd) {
            shader.AddLine(subroutine.GetName() + "();");
            shader.AddLine("return true;");
        } else if (subroutine.exit_method == ExitMethod::Conditional) {
            shader.AddLine("if (" + subroutine.GetName() + "()) { return true; }");
        } else {
            shader.AddLine(subroutine.GetName() + "();");
        }
    }

    /**
     * Writes code that does an assignment operation.
     * @param reg the destination register code.
     * @param value the code representing the value to assign.
     */
    void SetDest(u64 elem, const std::string& reg, const std::string& value,
                 u64 dest_num_components, u64 value_num_components, bool is_abs = false) {
        std::string swizzle = ".";
        swizzle += "xyzw"[elem];

        std::string dest = reg + (dest_num_components != 1 ? swizzle : "");
        std::string src = "(" + value + ")" + (value_num_components != 1 ? swizzle : "");
        src = is_abs ? "abs(" + src + ")" : src;

        shader.AddLine(dest + " = " + src + ";");
    }

    /**
     * Compiles a single instruction from Tegra to GLSL.
     * @param offset the offset of the Tegra shader instruction.
     * @return the offset of the next instruction to execute. Usually it is the current offset
     * + 1. If the current instruction always terminates the program, returns PROGRAM_END.
     */
    u32 CompileInstr(u32 offset) {
        const Instruction instr = {program_code[offset]};

        shader.AddLine("// " + std::to_string(offset) + ": " + OpCode::GetInfo(instr.opcode).name);

        switch (OpCode::GetInfo(instr.opcode).type) {
        case OpCode::Type::Arithmetic: {
            std::string dest = GetRegister(instr.gpr0);
            std::string op_a = instr.alu.negate_a ? "-" : "";
            op_a += GetRegister(instr.gpr8);
            if (instr.alu.abs_a) {
                op_a = "abs(" + op_a + ")";
            }

            std::string op_b = instr.alu.negate_b ? "-" : "";

            if (instr.is_b_imm) {
                op_b += GetImmediate(instr);
            } else {
                if (instr.is_b_gpr) {
                    op_b += GetRegister(instr.gpr20);
                } else {
                    op_b += GetUniform(instr.uniform);
                }
            }

            if (instr.alu.abs_b) {
                op_b = "abs(" + op_b + ")";
            }

            switch (instr.opcode.EffectiveOpCode()) {
            case OpCode::Id::FMUL_C:
            case OpCode::Id::FMUL_R:
            case OpCode::Id::FMUL_IMM: {
                SetDest(0, dest, op_a + " * " + op_b, 1, 1, instr.alu.abs_d);
                break;
            }
            case OpCode::Id::FADD_C:
            case OpCode::Id::FADD_R:
            case OpCode::Id::FADD_IMM: {
                SetDest(0, dest, op_a + " + " + op_b, 1, 1, instr.alu.abs_d);
                break;
            }
            case OpCode::Id::MUFU: {
                switch (instr.sub_op) {
                case SubOp::Cos:
                    SetDest(0, dest, "cos(" + op_a + ")", 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Sin:
                    SetDest(0, dest, "sin(" + op_a + ")", 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Ex2:
                    SetDest(0, dest, "exp2(" + op_a + ")", 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Lg2:
                    SetDest(0, dest, "log2(" + op_a + ")", 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Rcp:
                    SetDest(0, dest, "1.0 / " + op_a, 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Rsq:
                    SetDest(0, dest, "inversesqrt(" + op_a + ")", 1, 1, instr.alu.abs_d);
                    break;
                case SubOp::Min:
                    SetDest(0, dest, "min(" + op_a + "," + op_b + ")", 1, 1, instr.alu.abs_d);
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled sub op: 0x%02x",
                                 static_cast<unsigned>(instr.sub_op.Value()));
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled arithmetic instruction: 0x%02x (%s): 0x%08x",
                             static_cast<unsigned>(instr.opcode.EffectiveOpCode()),
                             OpCode::GetInfo(instr.opcode).name.c_str(), instr.hex);
                break;
            }
            }
            break;
        }
        case OpCode::Type::Ffma: {
            std::string dest = GetRegister(instr.gpr0);
            std::string op_a = GetRegister(instr.gpr8);
            std::string op_b = instr.ffma.negate_b ? "-" : "";
            std::string op_c = instr.ffma.negate_c ? "-" : "";

            switch (instr.opcode.EffectiveOpCode()) {
            case OpCode::Id::FFMA_CR: {
                op_b += GetUniform(instr.uniform);
                op_c += GetRegister(instr.gpr39);
                break;
            }
            case OpCode::Id::FFMA_RR: {
                op_b += GetRegister(instr.gpr20);
                op_c += GetRegister(instr.gpr39);
                break;
            }
            case OpCode::Id::FFMA_RC: {
                op_b += GetRegister(instr.gpr39);
                op_c += GetUniform(instr.uniform);
                break;
            }
            case OpCode::Id::FFMA_IMM: {
                op_b += GetImmediate(instr);
                op_c += GetRegister(instr.gpr39);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled arithmetic FFMA instruction: 0x%02x (%s): 0x%08x",
                             static_cast<unsigned>(instr.opcode.EffectiveOpCode()),
                             OpCode::GetInfo(instr.opcode).name.c_str(), instr.hex);
                throw DecompileFail("Unhandled instruction");
                break;
            }
            }

            SetDest(0, dest, op_a + " * " + op_b + " + " + op_c, 1, 1);
            break;
        }
        case OpCode::Type::Memory: {
            std::string gpr0 = GetRegister(instr.gpr0);
            const Attribute::Index attribute = instr.attribute.fmt20.index;

            switch (instr.opcode.EffectiveOpCode()) {
            case OpCode::Id::LD_A: {
                ASSERT_MSG(instr.attribute.fmt20.size == 0, "untested");
                SetDest(instr.attribute.fmt20.element, gpr0, GetInputAttribute(attribute), 1, 4);
                break;
            }
            case OpCode::Id::ST_A: {
                ASSERT_MSG(instr.attribute.fmt20.size == 0, "untested");
                SetDest(instr.attribute.fmt20.element, GetOutputAttribute(attribute), gpr0, 4, 1);
                break;
            }
            case OpCode::Id::TEXS: {
                ASSERT_MSG(instr.attribute.fmt20.size == 4, "untested");
                const std::string op_a = GetRegister(instr.gpr8);
                const std::string op_b = GetRegister(instr.gpr20);
                const std::string sampler = GetSampler(instr.sampler);
                const std::string coord = "vec2(" + op_a + ", " + op_b + ")";
                const std::string texture = "texture(" + sampler + ", " + coord + ")";
                for (unsigned elem = 0; elem < instr.attribute.fmt20.size; ++elem) {
                    SetDest(elem, GetRegister(instr.gpr0, elem), texture, 1, 4);
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled memory instruction: 0x%02x (%s): 0x%08x",
                             static_cast<unsigned>(instr.opcode.EffectiveOpCode()),
                             OpCode::GetInfo(instr.opcode).name.c_str(), instr.hex);
                throw DecompileFail("Unhandled instruction");
                break;
            }
            }
            break;
        }

        default: {
            switch (instr.opcode.EffectiveOpCode()) {
            case OpCode::Id::EXIT: {
                shader.AddLine("return true;");
                offset = PROGRAM_END - 1;
                break;
            }
            case OpCode::Id::IPA: {
                const auto& attribute = instr.attribute.fmt28;

                if (attribute.index == Attribute::Index::Position) {
                    LOG_CRITICAL(HW_GPU, "Unimplemented");
                    break;
                }

                std::string dest = GetRegister(instr.gpr0);
                SetDest(attribute.element, dest, GetInputAttribute(attribute.index), 1, 4);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled instruction: 0x%02x (%s): 0x%08x",
                             static_cast<unsigned>(instr.opcode.EffectiveOpCode()),
                             OpCode::GetInfo(instr.opcode).name.c_str(), instr.hex);
                break;
            }
            }

            break;
        }
        }

        return offset + 1;
    }

    /**
     * Compiles a range of instructions from Tegra to GLSL.
     * @param begin the offset of the starting instruction.
     * @param end the offset where the compilation should stop (exclusive).
     * @return the offset of the next instruction to compile. PROGRAM_END if the program
     * terminates.
     */
    u32 CompileRange(u32 begin, u32 end) {
        u32 program_counter;
        for (program_counter = begin; program_counter < (begin > end ? PROGRAM_END : end);) {
            program_counter = CompileInstr(program_counter);
        }
        return program_counter;
    }

    void Generate() {
        // Add declarations for all subroutines
        for (const auto& subroutine : subroutines) {
            shader.AddLine("bool " + subroutine.GetName() + "();");
        }
        shader.AddLine("");

        // Add the main entry point
        shader.AddLine("bool exec_shader() {");
        ++shader.scope;
        CallSubroutine(GetSubroutine(main_offset, PROGRAM_END));
        --shader.scope;
        shader.AddLine("}\n");

        // Add definitions for all subroutines
        for (const auto& subroutine : subroutines) {
            std::set<u32> labels = subroutine.labels;

            shader.AddLine("bool " + subroutine.GetName() + "() {");
            ++shader.scope;

            if (labels.empty()) {
                if (CompileRange(subroutine.begin, subroutine.end) != PROGRAM_END) {
                    shader.AddLine("return false;");
                }
            } else {
                labels.insert(subroutine.begin);
                shader.AddLine("uint jmp_to = " + std::to_string(subroutine.begin) + "u;");
                shader.AddLine("while (true) {");
                ++shader.scope;

                shader.AddLine("switch (jmp_to) {");

                for (auto label : labels) {
                    shader.AddLine("case " + std::to_string(label) + "u: {");
                    ++shader.scope;

                    auto next_it = labels.lower_bound(label + 1);
                    u32 next_label = next_it == labels.end() ? subroutine.end : *next_it;

                    u32 compile_end = CompileRange(label, next_label);
                    if (compile_end > next_label && compile_end != PROGRAM_END) {
                        // This happens only when there is a label inside a IF/LOOP block
                        shader.AddLine("{ jmp_to = " + std::to_string(compile_end) + "u; break; }");
                        labels.emplace(compile_end);
                    }

                    --shader.scope;
                    shader.AddLine("}");
                }

                shader.AddLine("default: return false;");
                shader.AddLine("}");

                --shader.scope;
                shader.AddLine("}");

                shader.AddLine("return false;");
            }

            --shader.scope;
            shader.AddLine("}\n");

            DEBUG_ASSERT(shader.scope == 0);
        }

        GenerateDeclarations();
    }

    /// Returns a list of constant buffer declarations
    std::vector<ConstBufferEntry> GetConstBuffersDeclarations() const {
        std::vector<ConstBufferEntry> result;
        std::copy_if(declr_const_buffers.begin(), declr_const_buffers.end(),
                     std::back_inserter(result), [](const auto& entry) { return entry.IsUsed(); });
        return result;
    }

    /// Add declarations for registers
    void GenerateDeclarations() {
        for (const auto& reg : declr_register) {
            declarations.AddLine("float " + reg + " = 0.0;");
        }
        declarations.AddLine("");

        for (const auto& index : declr_input_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            declarations.AddLine("layout(location = " +
                                 std::to_string(static_cast<u32>(index) -
                                                static_cast<u32>(Attribute::Index::Attribute_0)) +
                                 ") in vec4 " + GetInputAttribute(index) + ";");
        }
        declarations.AddLine("");

        for (const auto& index : declr_output_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            declarations.AddLine("layout(location = " +
                                 std::to_string(static_cast<u32>(index) -
                                                static_cast<u32>(Attribute::Index::Attribute_0)) +
                                 ") out vec4 " + GetOutputAttribute(index) + ";");
        }
        declarations.AddLine("");

        unsigned const_buffer_layout = 0;
        for (const auto& entry : GetConstBuffersDeclarations()) {
            declarations.AddLine("layout(std430) buffer " + entry.GetName());
            declarations.AddLine("{");
            declarations.AddLine("    float c" + std::to_string(entry.GetIndex()) + "[];");
            declarations.AddLine("};");
            declarations.AddLine("");
            ++const_buffer_layout;
        }
    }

private:
    const std::set<Subroutine>& subroutines;
    const ProgramCode& program_code;
    const u32 main_offset;
    Maxwell3D::Regs::ShaderStage stage;

    ShaderWriter shader;
    ShaderWriter declarations;

    // Declarations
    std::set<std::string> declr_register;
    std::set<Attribute::Index> declr_input_attribute;
    std::set<Attribute::Index> declr_output_attribute;
    std::array<ConstBufferEntry, Maxwell3D::Regs::MaxConstBuffers> declr_const_buffers;
};

std::string GetCommonDeclarations() {
    return "bool exec_shader();";
}

boost::optional<ProgramResult> DecompileProgram(const ProgramCode& program_code, u32 main_offset,
                                                Maxwell3D::Regs::ShaderStage stage) {
    try {
        auto subroutines = ControlFlowAnalyzer(program_code, main_offset).GetSubroutines();
        GLSLGenerator generator(subroutines, program_code, main_offset, stage);
        return ProgramResult{generator.GetShaderCode(), generator.GetEntries()};
    } catch (const DecompileFail& exception) {
        LOG_ERROR(HW_GPU, "Shader decompilation failed: %s", exception.what());
    }
    return boost::none;
}

} // namespace Decompiler
} // namespace GLShader
