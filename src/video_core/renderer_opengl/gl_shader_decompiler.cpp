// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <set>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"

namespace OpenGL::GLShader::Decompiler {

using Tegra::Shader::Attribute;
using Tegra::Shader::Instruction;
using Tegra::Shader::LogicOperation;
using Tegra::Shader::OpCode;
using Tegra::Shader::Register;
using Tegra::Shader::Sampler;
using Tegra::Shader::SubOp;

constexpr u32 PROGRAM_END = MAX_PROGRAM_CODE_LENGTH;
constexpr u32 PROGRAM_HEADER_SIZE = 0x50;

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
        return "sub_" + std::to_string(begin) + '_' + std::to_string(end) + '_' + suffix;
    }

    u32 begin;                 ///< Entry point of the subroutine.
    u32 end;                   ///< Return point of the subroutine.
    const std::string& suffix; ///< Suffix of the shader, used to make a unique subroutine name
    ExitMethod exit_method;    ///< Exit method of the subroutine.
    std::set<u32> labels;      ///< Addresses refereced by JMP instructions.

    bool operator<(const Subroutine& rhs) const {
        return std::tie(begin, end) < std::tie(rhs.begin, rhs.end);
    }
};

/// Analyzes shader code and produces a set of subroutines.
class ControlFlowAnalyzer {
public:
    ControlFlowAnalyzer(const ProgramCode& program_code, u32 main_offset, const std::string& suffix)
        : program_code(program_code) {

        // Recursively finds all subroutines.
        const Subroutine& program_main = AddSubroutine(main_offset, PROGRAM_END, suffix);
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
    const Subroutine& AddSubroutine(u32 begin, u32 end, const std::string& suffix) {
        Subroutine subroutine{begin, end, suffix, ExitMethod::Undetermined, {}};

        const auto iter = subroutines.find(subroutine);
        if (iter != subroutines.end()) {
            return *iter;
        }

        subroutine.exit_method = Scan(begin, end, subroutine.labels);
        if (subroutine.exit_method == ExitMethod::Undetermined) {
            throw DecompileFail("Recursive function detected");
        }

        return *subroutines.insert(std::move(subroutine)).first;
    }

    /// Merges exit method of two parallel branches.
    static ExitMethod ParallelExit(ExitMethod a, ExitMethod b) {
        if (a == ExitMethod::Undetermined) {
            return b;
        }
        if (b == ExitMethod::Undetermined) {
            return a;
        }
        if (a == b) {
            return a;
        }
        return ExitMethod::Conditional;
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
            if (const auto opcode = OpCode::Decode(instr)) {
                switch (opcode->GetId()) {
                case OpCode::Id::EXIT: {
                    // The EXIT instruction can be predicated, which means that the shader can
                    // conditionally end on this instruction. We have to consider the case where the
                    // condition is not met and check the exit method of that other basic block.
                    using Tegra::Shader::Pred;
                    if (instr.pred.pred_index == static_cast<u64>(Pred::UnusedIndex)) {
                        return exit_method = ExitMethod::AlwaysEnd;
                    } else {
                        ExitMethod not_met = Scan(offset + 1, end, labels);
                        return exit_method = ParallelExit(ExitMethod::AlwaysEnd, not_met);
                    }
                }
                case OpCode::Id::BRA: {
                    u32 target = offset + instr.bra.GetBranchTarget();
                    labels.insert(target);
                    ExitMethod no_jmp = Scan(offset + 1, end, labels);
                    ExitMethod jmp = Scan(target, end, labels);
                    return exit_method = ParallelExit(no_jmp, jmp);
                }
                case OpCode::Id::SSY: {
                    // The SSY instruction uses a similar encoding as the BRA instruction.
                    ASSERT_MSG(instr.bra.constant_buffer == 0,
                               "Constant buffer SSY is not supported");
                    u32 target = offset + instr.bra.GetBranchTarget();
                    labels.insert(target);
                    // Continue scanning for an exit method.
                    break;
                }
                }
            }
        }
        return exit_method = ExitMethod::AlwaysReturn;
    }
};

class ShaderWriter {
public:
    void AddLine(std::string_view text) {
        DEBUG_ASSERT(scope >= 0);
        if (!text.empty()) {
            AppendIndentation();
        }
        shader_source += text;
        AddNewLine();
    }

    void AddLine(char character) {
        DEBUG_ASSERT(scope >= 0);
        AppendIndentation();
        shader_source += character;
        AddNewLine();
    }

    void AddNewLine() {
        DEBUG_ASSERT(scope >= 0);
        shader_source += '\n';
    }

    std::string GetResult() {
        return std::move(shader_source);
    }

    int scope = 0;

private:
    void AppendIndentation() {
        shader_source.append(static_cast<size_t>(scope) * 4, ' ');
    }

    std::string shader_source;
};

/**
 * Represents an emulated shader register, used to track the state of that register for emulation
 * with GLSL. At this time, a register can be used as a float or an integer. This class is used for
 * bookkeeping within the GLSL program.
 */
class GLSLRegister {
public:
    enum class Type {
        Float,
        Integer,
        UnsignedInteger,
    };

    GLSLRegister(size_t index, const std::string& suffix) : index{index}, suffix{suffix} {}

    /// Gets the GLSL type string for a register
    static std::string GetTypeString() {
        return "float";
    }

    /// Gets the GLSL register prefix string, used for declarations and referencing
    static std::string GetPrefixString() {
        return "reg_";
    }

    /// Returns a GLSL string representing the current state of the register
    std::string GetString() const {
        return GetPrefixString() + std::to_string(index) + '_' + suffix;
    }

    /// Returns the index of the register
    size_t GetIndex() const {
        return index;
    }

private:
    const size_t index;
    const std::string& suffix;
};

/**
 * Used to manage shader registers that are emulated with GLSL. This class keeps track of the state
 * of all registers (e.g. whether they are currently being used as Floats or Integers), and
 * generates the necessary GLSL code to perform conversions as needed. This class is used for
 * bookkeeping within the GLSL program.
 */
class GLSLRegisterManager {
public:
    GLSLRegisterManager(ShaderWriter& shader, ShaderWriter& declarations,
                        const Maxwell3D::Regs::ShaderStage& stage, const std::string& suffix)
        : shader{shader}, declarations{declarations}, stage{stage}, suffix{suffix} {
        BuildRegisterList();
        BuildInputList();
    }

    /**
     * Returns code that does an integer size conversion for the specified size.
     * @param value Value to perform integer size conversion on.
     * @param size Register size to use for conversion instructions.
     * @returns GLSL string corresponding to the value converted to the specified size.
     */
    static std::string ConvertIntegerSize(const std::string& value, Register::Size size) {
        switch (size) {
        case Register::Size::Byte:
            return "((" + value + " << 24) >> 24)";
        case Register::Size::Short:
            return "((" + value + " << 16) >> 16)";
        case Register::Size::Word:
            // Default - do nothing
            return value;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented conversion size {}", static_cast<u32>(size));
            UNREACHABLE();
        }
    }

    /**
     * Gets a register as an float.
     * @param reg The register to get.
     * @param elem The element to use for the operation.
     * @returns GLSL string corresponding to the register as a float.
     */
    std::string GetRegisterAsFloat(const Register& reg, unsigned elem = 0) {
        return GetRegister(reg, elem);
    }

    /**
     * Gets a register as an integer.
     * @param reg The register to get.
     * @param elem The element to use for the operation.
     * @param is_signed Whether to get the register as a signed (or unsigned) integer.
     * @param size Register size to use for conversion instructions.
     * @returns GLSL string corresponding to the register as an integer.
     */
    std::string GetRegisterAsInteger(const Register& reg, unsigned elem = 0, bool is_signed = true,
                                     Register::Size size = Register::Size::Word) {
        const std::string func{is_signed ? "floatBitsToInt" : "floatBitsToUint"};
        const std::string value{func + '(' + GetRegister(reg, elem) + ')'};
        return ConvertIntegerSize(value, size);
    }

    /**
     * Writes code that does a register assignment to float value operation.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param value The code representing the value to assign.
     * @param dest_num_components Number of components in the destination.
     * @param value_num_components Number of components in the value.
     * @param is_saturated Optional, when True, saturates the provided value.
     * @param dest_elem Optional, the destination element to use for the operation.
     */
    void SetRegisterToFloat(const Register& reg, u64 elem, const std::string& value,
                            u64 dest_num_components, u64 value_num_components,
                            bool is_saturated = false, u64 dest_elem = 0) {

        SetRegister(reg, elem, is_saturated ? "clamp(" + value + ", 0.0, 1.0)" : value,
                    dest_num_components, value_num_components, dest_elem);
    }

    /**
     * Writes code that does a register assignment to integer value operation.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param value The code representing the value to assign.
     * @param dest_num_components Number of components in the destination.
     * @param value_num_components Number of components in the value.
     * @param is_saturated Optional, when True, saturates the provided value.
     * @param dest_elem Optional, the destination element to use for the operation.
     * @param size Register size to use for conversion instructions.
     */
    void SetRegisterToInteger(const Register& reg, bool is_signed, u64 elem,
                              const std::string& value, u64 dest_num_components,
                              u64 value_num_components, bool is_saturated = false,
                              u64 dest_elem = 0, Register::Size size = Register::Size::Word) {
        ASSERT_MSG(!is_saturated, "Unimplemented");

        const std::string func{is_signed ? "intBitsToFloat" : "uintBitsToFloat"};

        SetRegister(reg, elem, func + '(' + ConvertIntegerSize(value, size) + ')',
                    dest_num_components, value_num_components, dest_elem);
    }

    /**
     * Writes code that does a register assignment to input attribute operation. Input attributes
     * are stored as floats, so this may require conversion.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param attribute The input attribute to use as the source value.
     */
    void SetRegisterToInputAttibute(const Register& reg, u64 elem, Attribute::Index attribute,
                                    const Tegra::Shader::IpaMode& input_mode) {
        std::string dest = GetRegisterAsFloat(reg);
        std::string src = GetInputAttribute(attribute, input_mode) + GetSwizzle(elem);
        shader.AddLine(dest + " = " + src + ';');
    }

    /**
     * Writes code that does a output attribute assignment to register operation. Output attributes
     * are stored as floats, so this may require conversion.
     * @param attribute The destination output attribute.
     * @param elem The element to use for the operation.
     * @param reg The register to use as the source value.
     */
    void SetOutputAttributeToRegister(Attribute::Index attribute, u64 elem, const Register& reg) {
        std::string dest = GetOutputAttribute(attribute);
        std::string src = GetRegisterAsFloat(reg);

        if (!dest.empty()) {
            // Can happen with unknown/unimplemented output attributes, in which case we ignore the
            // instruction for now.
            shader.AddLine(dest + GetSwizzle(elem) + " = " + src + ';');
        }
    }

    /// Generates code representing a uniform (C buffer) register, interpreted as the input type.
    std::string GetUniform(u64 index, u64 offset, GLSLRegister::Type type,
                           Register::Size size = Register::Size::Word) {
        declr_const_buffers[index].MarkAsUsed(index, offset, stage);
        std::string value = 'c' + std::to_string(index) + '[' + std::to_string(offset / 4) + "][" +
                            std::to_string(offset % 4) + ']';

        if (type == GLSLRegister::Type::Float) {
            // Do nothing, default
        } else if (type == GLSLRegister::Type::Integer) {
            value = "floatBitsToInt(" + value + ')';
        } else if (type == GLSLRegister::Type::UnsignedInteger) {
            value = "floatBitsToUint(" + value + ')';
        } else {
            UNREACHABLE();
        }

        return ConvertIntegerSize(value, size);
    }

    std::string GetUniformIndirect(u64 cbuf_index, s64 offset, const std::string& index_str,
                                   GLSLRegister::Type type) {
        declr_const_buffers[cbuf_index].MarkAsUsedIndirect(cbuf_index, stage);

        std::string final_offset = fmt::format("({} + {})", index_str, offset / 4);
        std::string value = 'c' + std::to_string(cbuf_index) + '[' + final_offset + " / 4][" +
                            final_offset + " % 4]";

        if (type == GLSLRegister::Type::Float) {
            return value;
        } else if (type == GLSLRegister::Type::Integer) {
            return "floatBitsToInt(" + value + ')';
        } else {
            UNREACHABLE();
        }
    }

    /// Add declarations for registers
    void GenerateDeclarations(const std::string& suffix) {
        for (const auto& reg : regs) {
            declarations.AddLine(GLSLRegister::GetTypeString() + ' ' + reg.GetPrefixString() +
                                 std::to_string(reg.GetIndex()) + '_' + suffix + " = 0;");
        }
        declarations.AddNewLine();

        for (const auto element : declr_input_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            u32 idx =
                static_cast<u32>(element.first) - static_cast<u32>(Attribute::Index::Attribute_0);
            declarations.AddLine("layout(location = " + std::to_string(idx) + ")" +
                                 GetInputFlags(element.first) + "in vec4 " +
                                 GetInputAttribute(element.first, element.second) + ';');
        }
        declarations.AddNewLine();

        for (const auto& index : declr_output_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            declarations.AddLine("layout(location = " +
                                 std::to_string(static_cast<u32>(index) -
                                                static_cast<u32>(Attribute::Index::Attribute_0)) +
                                 ") out vec4 " + GetOutputAttribute(index) + ';');
        }
        declarations.AddNewLine();

        for (const auto& entry : GetConstBuffersDeclarations()) {
            declarations.AddLine("layout(std140) uniform " + entry.GetName());
            declarations.AddLine('{');
            declarations.AddLine("    vec4 c" + std::to_string(entry.GetIndex()) +
                                 "[MAX_CONSTBUFFER_ELEMENTS];");
            declarations.AddLine("};");
            declarations.AddNewLine();
        }
        declarations.AddNewLine();

        const auto& samplers = GetSamplers();
        for (const auto& sampler : samplers) {
            declarations.AddLine("uniform " + sampler.GetTypeString() + ' ' + sampler.GetName() +
                                 ';');
        }
        declarations.AddNewLine();
    }

    /// Returns a list of constant buffer declarations
    std::vector<ConstBufferEntry> GetConstBuffersDeclarations() const {
        std::vector<ConstBufferEntry> result;
        std::copy_if(declr_const_buffers.begin(), declr_const_buffers.end(),
                     std::back_inserter(result), [](const auto& entry) { return entry.IsUsed(); });
        return result;
    }

    /// Returns a list of samplers used in the shader
    const std::vector<SamplerEntry>& GetSamplers() const {
        return used_samplers;
    }

    /// Returns the GLSL sampler used for the input shader sampler, and creates a new one if
    /// necessary.
    std::string AccessSampler(const Sampler& sampler, Tegra::Shader::TextureType type,
                              bool is_array) {
        size_t offset = static_cast<size_t>(sampler.index.Value());

        // If this sampler has already been used, return the existing mapping.
        auto itr =
            std::find_if(used_samplers.begin(), used_samplers.end(),
                         [&](const SamplerEntry& entry) { return entry.GetOffset() == offset; });

        if (itr != used_samplers.end()) {
            ASSERT(itr->GetType() == type && itr->IsArray() == is_array);
            return itr->GetName();
        }

        // Otherwise create a new mapping for this sampler
        size_t next_index = used_samplers.size();
        SamplerEntry entry{stage, offset, next_index, type, is_array};
        used_samplers.emplace_back(entry);
        return entry.GetName();
    }

private:
    /// Generates code representing a temporary (GPR) register.
    std::string GetRegister(const Register& reg, unsigned elem) {
        if (reg == Register::ZeroIndex) {
            return "0";
        }

        return regs[reg.GetSwizzledIndex(elem)].GetString();
    }

    /**
     * Writes code that does a register assignment to value operation.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param value The code representing the value to assign.
     * @param dest_num_components Number of components in the destination.
     * @param value_num_components Number of components in the value.
     * @param dest_elem Optional, the destination element to use for the operation.
     */
    void SetRegister(const Register& reg, u64 elem, const std::string& value,
                     u64 dest_num_components, u64 value_num_components, u64 dest_elem) {
        if (reg == Register::ZeroIndex) {
            LOG_CRITICAL(HW_GPU, "Cannot set Register::ZeroIndex");
            UNREACHABLE();
            return;
        }

        std::string dest = GetRegister(reg, static_cast<u32>(dest_elem));
        if (dest_num_components > 1) {
            dest += GetSwizzle(elem);
        }

        std::string src = '(' + value + ')';
        if (value_num_components > 1) {
            src += GetSwizzle(elem);
        }

        shader.AddLine(dest + " = " + src + ';');
    }

    /// Build the GLSL register list.
    void BuildRegisterList() {
        regs.reserve(Register::NumRegisters);

        for (size_t index = 0; index < Register::NumRegisters; ++index) {
            regs.emplace_back(index, suffix);
        }
    }

    void BuildInputList() {
        const u32 size = static_cast<u32>(Attribute::Index::Attribute_31) -
                         static_cast<u32>(Attribute::Index::Attribute_0) + 1;
        declr_input_attribute.reserve(size);
    }

    /// Generates code representing an input attribute register.
    std::string GetInputAttribute(Attribute::Index attribute,
                                  const Tegra::Shader::IpaMode& input_mode) {
        switch (attribute) {
        case Attribute::Index::Position:
            if (stage != Maxwell3D::Regs::ShaderStage::Fragment) {
                return "position";
            } else {
                return "vec4(gl_FragCoord.x, gl_FragCoord.y, gl_FragCoord.z, 1.0)";
            }
        case Attribute::Index::PointCoord:
            return "vec4(gl_PointCoord.x, gl_PointCoord.y, 0, 0)";
        case Attribute::Index::TessCoordInstanceIDVertexID:
            // TODO(Subv): Find out what the values are for the first two elements when inside a
            // vertex shader, and what's the value of the fourth element when inside a Tess Eval
            // shader.
            ASSERT(stage == Maxwell3D::Regs::ShaderStage::Vertex);
            return "vec4(0, 0, uintBitsToFloat(instance_id.x), uintBitsToFloat(gl_VertexID))";
        case Attribute::Index::FrontFacing:
            // TODO(Subv): Find out what the values are for the other elements.
            ASSERT(stage == Maxwell3D::Regs::ShaderStage::Fragment);
            return "vec4(0, 0, 0, uintBitsToFloat(gl_FrontFacing ? 1 : 0))";
        default:
            const u32 index{static_cast<u32>(attribute) -
                            static_cast<u32>(Attribute::Index::Attribute_0)};
            if (attribute >= Attribute::Index::Attribute_0 &&
                attribute <= Attribute::Index::Attribute_31) {
                if (declr_input_attribute.count(attribute) == 0) {
                    declr_input_attribute[attribute] = input_mode;
                } else {
                    if (declr_input_attribute[attribute] != input_mode) {
                        LOG_CRITICAL(HW_GPU, "Same Input multiple input modes");
                        UNREACHABLE();
                    }
                }
                return "input_attribute_" + std::to_string(index);
            }

            LOG_CRITICAL(HW_GPU, "Unhandled input attribute: {}", static_cast<u32>(attribute));
            UNREACHABLE();
        }

        return "vec4(0, 0, 0, 0)";
    }

    std::string GetInputFlags(const Attribute::Index attribute) {
        const Tegra::Shader::IpaSampleMode sample_mode =
            declr_input_attribute[attribute].sampling_mode;
        const Tegra::Shader::IpaInterpMode interp_mode =
            declr_input_attribute[attribute].interpolation_mode;
        std::string out;
        switch (interp_mode) {
        case Tegra::Shader::IpaInterpMode::Flat: {
            out += "flat ";
            break;
        }
        case Tegra::Shader::IpaInterpMode::Linear: {
            out += "noperspective ";
            break;
        }
        case Tegra::Shader::IpaInterpMode::Perspective: {
            // Default, Smooth
            break;
        }
        default: {
            LOG_CRITICAL(HW_GPU, "Unhandled Ipa InterpMode: {}", static_cast<u32>(interp_mode));
            UNREACHABLE();
        }
        }
        switch (sample_mode) {
        case Tegra::Shader::IpaSampleMode::Centroid: {
            // Note not implemented, it can be implemented with the "centroid " keyword in glsl;
            LOG_CRITICAL(HW_GPU, "Ipa Sampler Mode: centroid, not implemented");
            UNREACHABLE();
            break;
        }
        case Tegra::Shader::IpaSampleMode::Default: {
            // Default, n/a
            break;
        }
        default: {
            LOG_CRITICAL(HW_GPU, "Unhandled Ipa SampleMode: {}", static_cast<u32>(sample_mode));
            UNREACHABLE();
        }
        }
        return out;
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

            LOG_CRITICAL(HW_GPU, "Unhandled output attribute: {}", index);
            UNREACHABLE();
            return {};
        }
    }

    /// Generates code to use for a swizzle operation.
    static std::string GetSwizzle(u64 elem) {
        ASSERT(elem <= 3);
        std::string swizzle = ".";
        swizzle += "xyzw"[elem];
        return swizzle;
    }

    ShaderWriter& shader;
    ShaderWriter& declarations;
    std::vector<GLSLRegister> regs;
    std::unordered_map<Attribute::Index, Tegra::Shader::IpaMode> declr_input_attribute;
    std::set<Attribute::Index> declr_output_attribute;
    std::array<ConstBufferEntry, Maxwell3D::Regs::MaxConstBuffers> declr_const_buffers;
    std::vector<SamplerEntry> used_samplers;
    const Maxwell3D::Regs::ShaderStage& stage;
    const std::string& suffix;
};

class GLSLGenerator {
public:
    GLSLGenerator(const std::set<Subroutine>& subroutines, const ProgramCode& program_code,
                  u32 main_offset, Maxwell3D::Regs::ShaderStage stage, const std::string& suffix)
        : subroutines(subroutines), program_code(program_code), main_offset(main_offset),
          stage(stage), suffix(suffix) {

        Generate(suffix);
    }

    std::string GetShaderCode() {
        return declarations.GetResult() + shader.GetResult();
    }

    /// Returns entries in the shader that are useful for external functions
    ShaderEntries GetEntries() const {
        return {regs.GetConstBuffersDeclarations(), regs.GetSamplers()};
    }

private:
    // Shader program header for a Fragment Shader.
    struct FragmentHeader {
        INSERT_PADDING_WORDS(5);
        INSERT_PADDING_WORDS(13);
        u32 enabled_color_outputs;
        union {
            BitField<0, 1, u32> writes_samplemask;
            BitField<1, 1, u32> writes_depth;
        };

        bool IsColorComponentOutputEnabled(u32 render_target, u32 component) const {
            u32 bit = render_target * 4 + component;
            return enabled_color_outputs & (1 << bit);
        }
    };
    static_assert(sizeof(FragmentHeader) == PROGRAM_HEADER_SIZE, "FragmentHeader size is wrong");

    /// Gets the Subroutine object corresponding to the specified address.
    const Subroutine& GetSubroutine(u32 begin, u32 end) const {
        auto iter = subroutines.find(Subroutine{begin, end, suffix});
        ASSERT(iter != subroutines.end());
        return *iter;
    }

    /// Generates code representing a 19-bit immediate value
    static std::string GetImmediate19(const Instruction& instr) {
        return fmt::format("uintBitsToFloat({})", instr.alu.GetImm20_19());
    }

    /// Generates code representing a 32-bit immediate value
    static std::string GetImmediate32(const Instruction& instr) {
        return fmt::format("uintBitsToFloat({})", instr.alu.GetImm20_32());
    }

    /// Generates code representing a texture sampler.
    std::string GetSampler(const Sampler& sampler, Tegra::Shader::TextureType type, bool is_array) {
        return regs.AccessSampler(sampler, type, is_array);
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

    /*
     * Writes code that assigns a predicate boolean variable.
     * @param pred The id of the predicate to write to.
     * @param value The expression value to assign to the predicate.
     */
    void SetPredicate(u64 pred, const std::string& value) {
        using Tegra::Shader::Pred;
        // Can't assign to the constant predicate.
        ASSERT(pred != static_cast<u64>(Pred::UnusedIndex));

        std::string variable = 'p' + std::to_string(pred) + '_' + suffix;
        shader.AddLine(variable + " = " + value + ';');
        declr_predicates.insert(std::move(variable));
    }

    /*
     * Returns the condition to use in the 'if' for a predicated instruction.
     * @param instr Instruction to generate the if condition for.
     * @returns string containing the predicate condition.
     */
    std::string GetPredicateCondition(u64 index, bool negate) {
        using Tegra::Shader::Pred;
        std::string variable;

        // Index 7 is used as an 'Always True' condition.
        if (index == static_cast<u64>(Pred::UnusedIndex)) {
            variable = "true";
        } else {
            variable = 'p' + std::to_string(index) + '_' + suffix;
            declr_predicates.insert(variable);
        }
        if (negate) {
            return "!(" + variable + ')';
        }

        return variable;
    }

    /**
     * Returns the comparison string to use to compare two values in the 'set' family of
     * instructions.
     * @param condition The condition used in the 'set'-family instruction.
     * @param op_a First operand to use for the comparison.
     * @param op_b Second operand to use for the comparison.
     * @returns String corresponding to the GLSL operator that matches the desired comparison.
     */
    std::string GetPredicateComparison(Tegra::Shader::PredCondition condition,
                                       const std::string& op_a, const std::string& op_b) const {
        using Tegra::Shader::PredCondition;
        static const std::unordered_map<PredCondition, const char*> PredicateComparisonStrings = {
            {PredCondition::LessThan, "<"},           {PredCondition::Equal, "=="},
            {PredCondition::LessEqual, "<="},         {PredCondition::GreaterThan, ">"},
            {PredCondition::NotEqual, "!="},          {PredCondition::GreaterEqual, ">="},
            {PredCondition::LessThanWithNan, "<"},    {PredCondition::NotEqualWithNan, "!="},
            {PredCondition::GreaterThanWithNan, ">"}, {PredCondition::GreaterEqualWithNan, ">="}};

        const auto& comparison{PredicateComparisonStrings.find(condition)};
        ASSERT_MSG(comparison != PredicateComparisonStrings.end(),
                   "Unknown predicate comparison operation");

        std::string predicate{'(' + op_a + ") " + comparison->second + " (" + op_b + ')'};
        if (condition == PredCondition::LessThanWithNan ||
            condition == PredCondition::NotEqualWithNan ||
            condition == PredCondition::GreaterThanWithNan ||
            condition == PredCondition::GreaterEqualWithNan) {
            predicate += " || isnan(" + op_a + ") || isnan(" + op_b + ')';
        }

        return predicate;
    }

    /**
     * Returns the operator string to use to combine two predicates in the 'setp' family of
     * instructions.
     * @params operation The operator used in the 'setp'-family instruction.
     * @returns String corresponding to the GLSL operator that matches the desired operator.
     */
    std::string GetPredicateCombiner(Tegra::Shader::PredOperation operation) const {
        using Tegra::Shader::PredOperation;
        static const std::unordered_map<PredOperation, const char*> PredicateOperationStrings = {
            {PredOperation::And, "&&"},
            {PredOperation::Or, "||"},
            {PredOperation::Xor, "^^"},
        };

        auto op = PredicateOperationStrings.find(operation);
        ASSERT_MSG(op != PredicateOperationStrings.end(), "Unknown predicate operation");
        return op->second;
    }

    /**
     * Transforms the input string GLSL operand into one that applies the abs() function and negates
     * the output if necessary. When both abs and neg are true, the negation will be applied after
     * taking the absolute value.
     * @param operand The input operand to take the abs() of, negate, or both.
     * @param abs Whether to apply the abs() function to the input operand.
     * @param neg Whether to negate the input operand.
     * @returns String corresponding to the operand after being transformed by the abs() and
     * negation operations.
     */
    static std::string GetOperandAbsNeg(const std::string& operand, bool abs, bool neg) {
        std::string result = operand;

        if (abs) {
            result = "abs(" + result + ')';
        }

        if (neg) {
            result = "-(" + result + ')';
        }

        return result;
    }

    /*
     * Returns whether the instruction at the specified offset is a 'sched' instruction.
     * Sched instructions always appear before a sequence of 3 instructions.
     */
    bool IsSchedInstruction(u32 offset) const {
        // sched instructions appear once every 4 instructions.
        static constexpr size_t SchedPeriod = 4;
        u32 absolute_offset = offset - main_offset;

        return (absolute_offset % SchedPeriod) == 0;
    }

    void WriteLogicOperation(Register dest, LogicOperation logic_op, const std::string& op_a,
                             const std::string& op_b,
                             Tegra::Shader::PredicateResultMode predicate_mode,
                             Tegra::Shader::Pred predicate) {
        std::string result{};
        switch (logic_op) {
        case LogicOperation::And: {
            result = '(' + op_a + " & " + op_b + ')';
            break;
        }
        case LogicOperation::Or: {
            result = '(' + op_a + " | " + op_b + ')';
            break;
        }
        case LogicOperation::Xor: {
            result = '(' + op_a + " ^ " + op_b + ')';
            break;
        }
        case LogicOperation::PassB: {
            result = op_b;
            break;
        }
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented logic operation: {}", static_cast<u32>(logic_op));
            UNREACHABLE();
        }

        if (dest != Tegra::Shader::Register::ZeroIndex) {
            regs.SetRegisterToInteger(dest, true, 0, result, 1, 1);
        }

        using Tegra::Shader::PredicateResultMode;
        // Write the predicate value depending on the predicate mode.
        switch (predicate_mode) {
        case PredicateResultMode::None:
            // Do nothing.
            return;
        case PredicateResultMode::NotZero:
            // Set the predicate to true if the result is not zero.
            SetPredicate(static_cast<u64>(predicate), '(' + result + ") != 0");
            break;
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented predicate result mode: {}",
                         static_cast<u32>(predicate_mode));
            UNREACHABLE();
        }
    }

    void WriteLop3Instruction(Register dest, const std::string& op_a, const std::string& op_b,
                              const std::string& op_c, const std::string& imm_lut) {
        if (dest == Tegra::Shader::Register::ZeroIndex) {
            return;
        }

        static constexpr std::array<const char*, 32> shift_amounts = {
            "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10",
            "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21",
            "22", "23", "24", "25", "26", "27", "28", "29", "30", "31"};

        std::string result;
        result += '(';

        for (size_t i = 0; i < shift_amounts.size(); ++i) {
            if (i)
                result += '|';
            result += "(((" + imm_lut + " >> (((" + op_c + " >> " + shift_amounts[i] +
                      ") & 1) | ((" + op_b + " >> " + shift_amounts[i] + ") & 1) << 1 | ((" + op_a +
                      " >> " + shift_amounts[i] + ") & 1) << 2)) & 1) << " + shift_amounts[i] + ")";
        }

        result += ')';

        regs.SetRegisterToInteger(dest, true, 0, result, 1, 1);
    }

    void WriteTexsInstruction(const Instruction& instr, const std::string& coord,
                              const std::string& texture) {
        // Add an extra scope and declare the texture coords inside to prevent
        // overwriting them in case they are used as outputs of the texs instruction.
        shader.AddLine('{');
        ++shader.scope;
        shader.AddLine(coord);

        // TEXS has two destination registers and a swizzle. The first two elements in the swizzle
        // go into gpr0+0 and gpr0+1, and the rest goes into gpr28+0 and gpr28+1

        ASSERT_MSG(instr.texs.nodep == 0, "TEXS nodep not implemented");

        size_t written_components = 0;
        for (u32 component = 0; component < 4; ++component) {
            if (!instr.texs.IsComponentEnabled(component)) {
                continue;
            }

            if (written_components < 2) {
                // Write the first two swizzle components to gpr0 and gpr0+1
                regs.SetRegisterToFloat(instr.gpr0, component, texture, 1, 4, false,
                                        written_components % 2);
            } else {
                ASSERT(instr.texs.HasTwoDestinations());
                // Write the rest of the swizzle components to gpr28 and gpr28+1
                regs.SetRegisterToFloat(instr.gpr28, component, texture, 1, 4, false,
                                        written_components % 2);
            }

            ++written_components;
        }

        --shader.scope;
        shader.AddLine('}');
    }

    /*
     * Emits code to push the input target address to the SSY address stack, incrementing the stack
     * top.
     */
    void EmitPushToSSYStack(u32 target) {
        shader.AddLine('{');
        ++shader.scope;
        shader.AddLine("ssy_stack[ssy_stack_top] = " + std::to_string(target) + "u;");
        shader.AddLine("ssy_stack_top++;");
        --shader.scope;
        shader.AddLine('}');
    }

    /*
     * Emits code to pop an address from the SSY address stack, setting the jump address to the
     * popped address and decrementing the stack top.
     */
    void EmitPopFromSSYStack() {
        shader.AddLine('{');
        ++shader.scope;
        shader.AddLine("ssy_stack_top--;");
        shader.AddLine("jmp_to = ssy_stack[ssy_stack_top];");
        shader.AddLine("break;");
        --shader.scope;
        shader.AddLine('}');
    }

    /// Writes the output values from a fragment shader to the corresponding GLSL output variables.
    void EmitFragmentOutputsWrite() {
        ASSERT(stage == Maxwell3D::Regs::ShaderStage::Fragment);
        FragmentHeader header;
        std::memcpy(&header, program_code.data(), PROGRAM_HEADER_SIZE);

        ASSERT_MSG(header.writes_samplemask == 0, "Samplemask write is unimplemented");

        // Write the color outputs using the data in the shader registers, disabled
        // rendertargets/components are skipped in the register assignment.
        u32 current_reg = 0;
        for (u32 render_target = 0; render_target < Maxwell3D::Regs::NumRenderTargets;
             ++render_target) {
            // TODO(Subv): Figure out how dual-source blending is configured in the Switch.
            for (u32 component = 0; component < 4; ++component) {
                if (header.IsColorComponentOutputEnabled(render_target, component)) {
                    shader.AddLine(fmt::format("color[{}][{}] = {};", render_target, component,
                                               regs.GetRegisterAsFloat(current_reg)));
                    ++current_reg;
                }
            }
        }

        if (header.writes_depth) {
            // The depth output is always 2 registers after the last color output, and current_reg
            // already contains one past the last color register.
            shader.AddLine("gl_FragDepth = " + regs.GetRegisterAsFloat(current_reg + 1) + ';');
        }
    }

    /**
     * Compiles a single instruction from Tegra to GLSL.
     * @param offset the offset of the Tegra shader instruction.
     * @return the offset of the next instruction to execute. Usually it is the current offset
     * + 1. If the current instruction always terminates the program, returns PROGRAM_END.
     */
    u32 CompileInstr(u32 offset) {
        // Ignore sched instructions when generating code.
        if (IsSchedInstruction(offset)) {
            return offset + 1;
        }

        const Instruction instr = {program_code[offset]};
        const auto opcode = OpCode::Decode(instr);

        // Decoding failure
        if (!opcode) {
            LOG_CRITICAL(HW_GPU, "Unhandled instruction: {0:x}", instr.value);
            UNREACHABLE();
            return offset + 1;
        }

        shader.AddLine("// " + std::to_string(offset) + ": " + opcode->GetName() + " (" +
                       std::to_string(instr.value) + ')');

        using Tegra::Shader::Pred;
        ASSERT_MSG(instr.pred.full_pred != Pred::NeverExecute,
                   "NeverExecute predicate not implemented");

        // Some instructions (like SSY) don't have a predicate field, they are always
        // unconditionally executed.
        bool can_be_predicated = OpCode::IsPredicatedInstruction(opcode->GetId());

        if (can_be_predicated && instr.pred.pred_index != static_cast<u64>(Pred::UnusedIndex)) {
            shader.AddLine("if (" +
                           GetPredicateCondition(instr.pred.pred_index, instr.negate_pred != 0) +
                           ')');
            shader.AddLine('{');
            ++shader.scope;
        }

        switch (opcode->GetType()) {
        case OpCode::Type::Arithmetic: {
            std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);

            std::string op_b;

            if (instr.is_b_imm) {
                op_b = GetImmediate19(instr);
            } else {
                if (instr.is_b_gpr) {
                    op_b = regs.GetRegisterAsFloat(instr.gpr20);
                } else {
                    op_b = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                           GLSLRegister::Type::Float);
                }
            }

            switch (opcode->GetId()) {
            case OpCode::Id::MOV_C:
            case OpCode::Id::MOV_R: {
                // MOV does not have neither 'abs' nor 'neg' bits.
                regs.SetRegisterToFloat(instr.gpr0, 0, op_b, 1, 1);
                break;
            }

            case OpCode::Id::FMUL_C:
            case OpCode::Id::FMUL_R:
            case OpCode::Id::FMUL_IMM: {
                // FMUL does not have 'abs' bits and only the second operand has a 'neg' bit.
                ASSERT_MSG(instr.fmul.tab5cb8_2 == 0, "FMUL tab5cb8_2({}) is not implemented",
                           instr.fmul.tab5cb8_2.Value());
                ASSERT_MSG(instr.fmul.tab5c68_1 == 0, "FMUL tab5cb8_1({}) is not implemented",
                           instr.fmul.tab5c68_1.Value());
                ASSERT_MSG(instr.fmul.tab5c68_0 == 1, "FMUL tab5cb8_0({}) is not implemented",
                           instr.fmul.tab5c68_0
                               .Value()); // SMO typical sends 1 here which seems to be the default
                ASSERT_MSG(instr.fmul.cc == 0, "FMUL cc is not implemented");

                op_b = GetOperandAbsNeg(op_b, false, instr.fmul.negate_b);
                regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " * " + op_b, 1, 1,
                                        instr.alu.saturate_d);
                break;
            }
            case OpCode::Id::FADD_C:
            case OpCode::Id::FADD_R:
            case OpCode::Id::FADD_IMM: {
                op_a = GetOperandAbsNeg(op_a, instr.alu.abs_a, instr.alu.negate_a);
                op_b = GetOperandAbsNeg(op_b, instr.alu.abs_b, instr.alu.negate_b);
                regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " + " + op_b, 1, 1,
                                        instr.alu.saturate_d);
                break;
            }
            case OpCode::Id::MUFU: {
                op_a = GetOperandAbsNeg(op_a, instr.alu.abs_a, instr.alu.negate_a);
                switch (instr.sub_op) {
                case SubOp::Cos:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "cos(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Sin:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "sin(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Ex2:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "exp2(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Lg2:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "log2(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Rcp:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "1.0 / " + op_a, 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Rsq:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "inversesqrt(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                case SubOp::Sqrt:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "sqrt(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d);
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled MUFU sub op: {0:x}",
                                 static_cast<unsigned>(instr.sub_op.Value()));
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::FMNMX_C:
            case OpCode::Id::FMNMX_R:
            case OpCode::Id::FMNMX_IMM: {
                op_a = GetOperandAbsNeg(op_a, instr.alu.abs_a, instr.alu.negate_a);
                op_b = GetOperandAbsNeg(op_b, instr.alu.abs_b, instr.alu.negate_b);

                std::string condition =
                    GetPredicateCondition(instr.alu.fmnmx.pred, instr.alu.fmnmx.negate_pred != 0);
                std::string parameters = op_a + ',' + op_b;
                regs.SetRegisterToFloat(instr.gpr0, 0,
                                        '(' + condition + ") ? min(" + parameters + ") : max(" +
                                            parameters + ')',
                                        1, 1);
                break;
            }
            case OpCode::Id::RRO_C:
            case OpCode::Id::RRO_R:
            case OpCode::Id::RRO_IMM: {
                // Currently RRO is only implemented as a register move.
                op_b = GetOperandAbsNeg(op_b, instr.alu.abs_b, instr.alu.negate_b);
                regs.SetRegisterToFloat(instr.gpr0, 0, op_b, 1, 1);
                LOG_WARNING(HW_GPU, "RRO instruction is incomplete");
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled arithmetic instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::ArithmeticImmediate: {
            switch (opcode->GetId()) {
            case OpCode::Id::MOV32_IMM: {
                regs.SetRegisterToFloat(instr.gpr0, 0, GetImmediate32(instr), 1, 1);
                break;
            }
            case OpCode::Id::FMUL32_IMM: {
                regs.SetRegisterToFloat(
                    instr.gpr0, 0,
                    regs.GetRegisterAsFloat(instr.gpr8) + " * " + GetImmediate32(instr), 1, 1);
                break;
            }
            case OpCode::Id::FADD32I: {
                std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);
                std::string op_b = GetImmediate32(instr);

                if (instr.fadd32i.abs_a) {
                    op_a = "abs(" + op_a + ')';
                }

                if (instr.fadd32i.negate_a) {
                    op_a = "-(" + op_a + ')';
                }

                if (instr.fadd32i.abs_b) {
                    op_b = "abs(" + op_b + ')';
                }

                if (instr.fadd32i.negate_b) {
                    op_b = "-(" + op_b + ')';
                }

                regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " + " + op_b, 1, 1);
                break;
            }
            }
            break;
        }
        case OpCode::Type::Bfe: {
            ASSERT_MSG(!instr.bfe.negate_b, "Unimplemented");

            std::string op_a = instr.bfe.negate_a ? "-" : "";
            op_a += regs.GetRegisterAsInteger(instr.gpr8);

            switch (opcode->GetId()) {
            case OpCode::Id::BFE_IMM: {
                std::string inner_shift =
                    '(' + op_a + " << " + std::to_string(instr.bfe.GetLeftShiftValue()) + ')';
                std::string outer_shift =
                    '(' + inner_shift + " >> " +
                    std::to_string(instr.bfe.GetLeftShiftValue() + instr.bfe.shift_position) + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, outer_shift, 1, 1);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled BFE instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }

            break;
        }

        case OpCode::Type::Shift: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8, 0, true);
            std::string op_b;

            if (instr.is_b_imm) {
                op_b += '(' + std::to_string(instr.alu.GetSignedImm20_20()) + ')';
            } else {
                if (instr.is_b_gpr) {
                    op_b += regs.GetRegisterAsInteger(instr.gpr20);
                } else {
                    op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                            GLSLRegister::Type::Integer);
                }
            }

            switch (opcode->GetId()) {
            case OpCode::Id::SHR_C:
            case OpCode::Id::SHR_R:
            case OpCode::Id::SHR_IMM: {
                if (!instr.shift.is_signed) {
                    // Logical shift right
                    op_a = "uint(" + op_a + ')';
                }

                // Cast to int is superfluous for arithmetic shift, it's only for a logical shift
                regs.SetRegisterToInteger(instr.gpr0, true, 0, "int(" + op_a + " >> " + op_b + ')',
                                          1, 1);
                break;
            }
            case OpCode::Id::SHL_C:
            case OpCode::Id::SHL_R:
            case OpCode::Id::SHL_IMM:
                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " << " + op_b, 1, 1);
                break;
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled shift instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }
            break;
        }

        case OpCode::Type::ArithmeticIntegerImmediate: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8);
            std::string op_b = std::to_string(instr.alu.imm20_32.Value());

            switch (opcode->GetId()) {
            case OpCode::Id::IADD32I:
                if (instr.iadd32i.negate_a)
                    op_a = "-(" + op_a + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " + " + op_b, 1, 1,
                                          instr.iadd32i.saturate != 0);
                break;
            case OpCode::Id::LOP32I: {
                if (instr.alu.lop32i.invert_a)
                    op_a = "~(" + op_a + ')';

                if (instr.alu.lop32i.invert_b)
                    op_b = "~(" + op_b + ')';

                WriteLogicOperation(instr.gpr0, instr.alu.lop32i.operation, op_a, op_b,
                                    Tegra::Shader::PredicateResultMode::None,
                                    Tegra::Shader::Pred::UnusedIndex);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled ArithmeticIntegerImmediate instruction: {}",
                             opcode->GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::ArithmeticInteger: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8);
            std::string op_b;
            if (instr.is_b_imm) {
                op_b += '(' + std::to_string(instr.alu.GetSignedImm20_20()) + ')';
            } else {
                if (instr.is_b_gpr) {
                    op_b += regs.GetRegisterAsInteger(instr.gpr20);
                } else {
                    op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                            GLSLRegister::Type::Integer);
                }
            }

            switch (opcode->GetId()) {
            case OpCode::Id::IADD_C:
            case OpCode::Id::IADD_R:
            case OpCode::Id::IADD_IMM: {
                if (instr.alu_integer.negate_a)
                    op_a = "-(" + op_a + ')';

                if (instr.alu_integer.negate_b)
                    op_b = "-(" + op_b + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " + " + op_b, 1, 1,
                                          instr.alu.saturate_d);
                break;
            }
            case OpCode::Id::IADD3_C:
            case OpCode::Id::IADD3_R:
            case OpCode::Id::IADD3_IMM: {
                std::string op_c = regs.GetRegisterAsInteger(instr.gpr39);

                auto apply_height = [](auto height, auto& oprand) {
                    switch (height) {
                    case Tegra::Shader::IAdd3Height::None:
                        break;
                    case Tegra::Shader::IAdd3Height::LowerHalfWord:
                        oprand = "((" + oprand + ") & 0xFFFF)";
                        break;
                    case Tegra::Shader::IAdd3Height::UpperHalfWord:
                        oprand = "((" + oprand + ") >> 16)";
                        break;
                    default:
                        LOG_CRITICAL(HW_GPU, "Unhandled IADD3 height: {}",
                                     static_cast<u32>(height.Value()));
                        UNREACHABLE();
                    }
                };

                if (opcode->GetId() == OpCode::Id::IADD3_R) {
                    apply_height(instr.iadd3.height_a, op_a);
                    apply_height(instr.iadd3.height_b, op_b);
                    apply_height(instr.iadd3.height_c, op_c);
                }

                if (instr.iadd3.neg_a)
                    op_a = "-(" + op_a + ')';

                if (instr.iadd3.neg_b)
                    op_b = "-(" + op_b + ')';

                if (instr.iadd3.neg_c)
                    op_c = "-(" + op_c + ')';

                std::string result;
                if (opcode->GetId() == OpCode::Id::IADD3_R) {
                    switch (instr.iadd3.mode) {
                    case Tegra::Shader::IAdd3Mode::RightShift:
                        // TODO(tech4me): According to
                        // https://envytools.readthedocs.io/en/latest/hw/graph/maxwell/cuda/int.html?highlight=iadd3
                        // The addition between op_a and op_b should be done in uint33, more
                        // investigation required
                        result = "(((" + op_a + " + " + op_b + ") >> 16) + " + op_c + ')';
                        break;
                    case Tegra::Shader::IAdd3Mode::LeftShift:
                        result = "(((" + op_a + " + " + op_b + ") << 16) + " + op_c + ')';
                        break;
                    default:
                        result = '(' + op_a + " + " + op_b + " + " + op_c + ')';
                        break;
                    }
                } else {
                    result = '(' + op_a + " + " + op_b + " + " + op_c + ')';
                }

                regs.SetRegisterToInteger(instr.gpr0, true, 0, result, 1, 1);
                break;
            }
            case OpCode::Id::ISCADD_C:
            case OpCode::Id::ISCADD_R:
            case OpCode::Id::ISCADD_IMM: {
                if (instr.alu_integer.negate_a)
                    op_a = "-(" + op_a + ')';

                if (instr.alu_integer.negate_b)
                    op_b = "-(" + op_b + ')';

                std::string shift = std::to_string(instr.alu_integer.shift_amount.Value());

                regs.SetRegisterToInteger(instr.gpr0, true, 0,
                                          "((" + op_a + " << " + shift + ") + " + op_b + ')', 1, 1);
                break;
            }
            case OpCode::Id::POPC_C:
            case OpCode::Id::POPC_R:
            case OpCode::Id::POPC_IMM: {
                if (instr.popc.invert) {
                    op_b = "~(" + op_b + ')';
                }
                regs.SetRegisterToInteger(instr.gpr0, true, 0, "bitCount(" + op_b + ')', 1, 1);
                break;
            }
            case OpCode::Id::SEL_C:
            case OpCode::Id::SEL_R:
            case OpCode::Id::SEL_IMM: {
                std::string condition =
                    GetPredicateCondition(instr.sel.pred, instr.sel.neg_pred != 0);
                regs.SetRegisterToInteger(instr.gpr0, true, 0,
                                          '(' + condition + ") ? " + op_a + " : " + op_b, 1, 1);
                break;
            }
            case OpCode::Id::LOP_C:
            case OpCode::Id::LOP_R:
            case OpCode::Id::LOP_IMM: {
                if (instr.alu.lop.invert_a)
                    op_a = "~(" + op_a + ')';

                if (instr.alu.lop.invert_b)
                    op_b = "~(" + op_b + ')';

                WriteLogicOperation(instr.gpr0, instr.alu.lop.operation, op_a, op_b,
                                    instr.alu.lop.pred_result_mode, instr.alu.lop.pred48);
                break;
            }
            case OpCode::Id::LOP3_C:
            case OpCode::Id::LOP3_R:
            case OpCode::Id::LOP3_IMM: {
                std::string op_c = regs.GetRegisterAsInteger(instr.gpr39);
                std::string lut;
                if (opcode->GetId() == OpCode::Id::LOP3_R) {
                    lut = '(' + std::to_string(instr.alu.lop3.GetImmLut28()) + ')';
                } else {
                    lut = '(' + std::to_string(instr.alu.lop3.GetImmLut48()) + ')';
                }

                WriteLop3Instruction(instr.gpr0, op_a, op_b, op_c, lut);
                break;
            }
            case OpCode::Id::IMNMX_C:
            case OpCode::Id::IMNMX_R:
            case OpCode::Id::IMNMX_IMM: {
                ASSERT_MSG(instr.imnmx.exchange == Tegra::Shader::IMinMaxExchange::None,
                           "Unimplemented");
                std::string condition =
                    GetPredicateCondition(instr.imnmx.pred, instr.imnmx.negate_pred != 0);
                std::string parameters = op_a + ',' + op_b;
                regs.SetRegisterToInteger(instr.gpr0, instr.imnmx.is_signed, 0,
                                          '(' + condition + ") ? min(" + parameters + ") : max(" +
                                              parameters + ')',
                                          1, 1);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled ArithmeticInteger instruction: {}",
                             opcode->GetName());
                UNREACHABLE();
            }
            }

            break;
        }
        case OpCode::Type::Ffma: {
            std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);
            std::string op_b = instr.ffma.negate_b ? "-" : "";
            std::string op_c = instr.ffma.negate_c ? "-" : "";

            ASSERT_MSG(instr.ffma.cc == 0, "FFMA cc not implemented");
            ASSERT_MSG(instr.ffma.tab5980_0 == 1, "FFMA tab5980_0({}) not implemented",
                       instr.ffma.tab5980_0.Value()); // Seems to be 1 by default based on SMO
            ASSERT_MSG(instr.ffma.tab5980_1 == 0, "FFMA tab5980_1({}) not implemented",
                       instr.ffma.tab5980_1.Value());

            switch (opcode->GetId()) {
            case OpCode::Id::FFMA_CR: {
                op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                        GLSLRegister::Type::Float);
                op_c += regs.GetRegisterAsFloat(instr.gpr39);
                break;
            }
            case OpCode::Id::FFMA_RR: {
                op_b += regs.GetRegisterAsFloat(instr.gpr20);
                op_c += regs.GetRegisterAsFloat(instr.gpr39);
                break;
            }
            case OpCode::Id::FFMA_RC: {
                op_b += regs.GetRegisterAsFloat(instr.gpr39);
                op_c += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                        GLSLRegister::Type::Float);
                break;
            }
            case OpCode::Id::FFMA_IMM: {
                op_b += GetImmediate19(instr);
                op_c += regs.GetRegisterAsFloat(instr.gpr39);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled FFMA instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }

            regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " * " + op_b + " + " + op_c, 1, 1,
                                    instr.alu.saturate_d);
            break;
        }
        case OpCode::Type::Conversion: {
            switch (opcode->GetId()) {
            case OpCode::Id::I2I_R: {
                ASSERT_MSG(!instr.conversion.selector, "Unimplemented");

                std::string op_a = regs.GetRegisterAsInteger(
                    instr.gpr20, 0, instr.conversion.is_input_signed, instr.conversion.src_size);

                if (instr.conversion.abs_a) {
                    op_a = "abs(" + op_a + ')';
                }

                if (instr.conversion.negate_a) {
                    op_a = "-(" + op_a + ')';
                }

                regs.SetRegisterToInteger(instr.gpr0, instr.conversion.is_output_signed, 0, op_a, 1,
                                          1, instr.alu.saturate_d, 0, instr.conversion.dest_size);
                break;
            }
            case OpCode::Id::I2F_R:
            case OpCode::Id::I2F_C: {
                ASSERT_MSG(instr.conversion.dest_size == Register::Size::Word, "Unimplemented");
                ASSERT_MSG(!instr.conversion.selector, "Unimplemented");

                std::string op_a{};

                if (instr.is_b_gpr) {
                    op_a =
                        regs.GetRegisterAsInteger(instr.gpr20, 0, instr.conversion.is_input_signed,
                                                  instr.conversion.src_size);
                } else {
                    op_a = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                           instr.conversion.is_input_signed
                                               ? GLSLRegister::Type::Integer
                                               : GLSLRegister::Type::UnsignedInteger,
                                           instr.conversion.src_size);
                }

                if (instr.conversion.abs_a) {
                    op_a = "abs(" + op_a + ')';
                }

                if (instr.conversion.negate_a) {
                    op_a = "-(" + op_a + ')';
                }

                regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1);
                break;
            }
            case OpCode::Id::F2F_R: {
                ASSERT_MSG(instr.conversion.dest_size == Register::Size::Word, "Unimplemented");
                ASSERT_MSG(instr.conversion.src_size == Register::Size::Word, "Unimplemented");
                std::string op_a = regs.GetRegisterAsFloat(instr.gpr20);

                if (instr.conversion.abs_a) {
                    op_a = "abs(" + op_a + ')';
                }

                if (instr.conversion.negate_a) {
                    op_a = "-(" + op_a + ')';
                }

                switch (instr.conversion.f2f.rounding) {
                case Tegra::Shader::F2fRoundingOp::None:
                    break;
                case Tegra::Shader::F2fRoundingOp::Round:
                    op_a = "roundEven(" + op_a + ')';
                    break;
                case Tegra::Shader::F2fRoundingOp::Floor:
                    op_a = "floor(" + op_a + ')';
                    break;
                case Tegra::Shader::F2fRoundingOp::Ceil:
                    op_a = "ceil(" + op_a + ')';
                    break;
                case Tegra::Shader::F2fRoundingOp::Trunc:
                    op_a = "trunc(" + op_a + ')';
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "Unimplemented f2f rounding mode {}",
                                 static_cast<u32>(instr.conversion.f2f.rounding.Value()));
                    UNREACHABLE();
                    break;
                }

                regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1, instr.alu.saturate_d);
                break;
            }
            case OpCode::Id::F2I_R:
            case OpCode::Id::F2I_C: {
                ASSERT_MSG(instr.conversion.src_size == Register::Size::Word, "Unimplemented");
                std::string op_a{};

                if (instr.is_b_gpr) {
                    op_a = regs.GetRegisterAsFloat(instr.gpr20);
                } else {
                    op_a = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                           GLSLRegister::Type::Float);
                }

                if (instr.conversion.abs_a) {
                    op_a = "abs(" + op_a + ')';
                }

                if (instr.conversion.negate_a) {
                    op_a = "-(" + op_a + ')';
                }

                switch (instr.conversion.f2i.rounding) {
                case Tegra::Shader::F2iRoundingOp::None:
                    break;
                case Tegra::Shader::F2iRoundingOp::Floor:
                    op_a = "floor(" + op_a + ')';
                    break;
                case Tegra::Shader::F2iRoundingOp::Ceil:
                    op_a = "ceil(" + op_a + ')';
                    break;
                case Tegra::Shader::F2iRoundingOp::Trunc:
                    op_a = "trunc(" + op_a + ')';
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "Unimplemented f2i rounding mode {}",
                                 static_cast<u32>(instr.conversion.f2i.rounding.Value()));
                    UNREACHABLE();
                    break;
                }

                if (instr.conversion.is_output_signed) {
                    op_a = "int(" + op_a + ')';
                } else {
                    op_a = "uint(" + op_a + ')';
                }

                regs.SetRegisterToInteger(instr.gpr0, instr.conversion.is_output_signed, 0, op_a, 1,
                                          1, false, 0, instr.conversion.dest_size);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled conversion instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::Memory: {
            switch (opcode->GetId()) {
            case OpCode::Id::LD_A: {
                ASSERT_MSG(instr.attribute.fmt20.size == 0, "untested");
                // Note: Shouldn't this be interp mode flat? As in no interpolation made.

                Tegra::Shader::IpaMode input_mode{Tegra::Shader::IpaInterpMode::Perspective,
                                                  Tegra::Shader::IpaSampleMode::Default};
                regs.SetRegisterToInputAttibute(instr.gpr0, instr.attribute.fmt20.element,
                                                instr.attribute.fmt20.index, input_mode);
                break;
            }
            case OpCode::Id::LD_C: {
                ASSERT_MSG(instr.ld_c.unknown == 0, "Unimplemented");

                // Add an extra scope and declare the index register inside to prevent
                // overwriting it in case it is used as an output of the LD instruction.
                shader.AddLine("{");
                ++shader.scope;

                shader.AddLine("uint index = (" + regs.GetRegisterAsInteger(instr.gpr8, 0, false) +
                               " / 4) & (MAX_CONSTBUFFER_ELEMENTS - 1);");

                std::string op_a =
                    regs.GetUniformIndirect(instr.cbuf36.index, instr.cbuf36.offset + 0, "index",
                                            GLSLRegister::Type::Float);

                switch (instr.ld_c.type.Value()) {
                case Tegra::Shader::UniformType::Single:
                    regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1);
                    break;

                case Tegra::Shader::UniformType::Double: {
                    std::string op_b =
                        regs.GetUniformIndirect(instr.cbuf36.index, instr.cbuf36.offset + 4,
                                                "index", GLSLRegister::Type::Float);
                    regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1);
                    regs.SetRegisterToFloat(instr.gpr0.Value() + 1, 0, op_b, 1, 1);
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled type: {}",
                                 static_cast<unsigned>(instr.ld_c.type.Value()));
                    UNREACHABLE();
                }

                --shader.scope;
                shader.AddLine("}");
                break;
            }
            case OpCode::Id::ST_A: {
                ASSERT_MSG(instr.attribute.fmt20.size == 0, "untested");
                regs.SetOutputAttributeToRegister(instr.attribute.fmt20.index,
                                                  instr.attribute.fmt20.element, instr.gpr0);
                break;
            }
            case OpCode::Id::TEX: {
                ASSERT_MSG(instr.tex.array == 0, "TEX arrays unimplemented");
                Tegra::Shader::TextureType texture_type{instr.tex.texture_type};
                std::string coord;

                switch (texture_type) {
                case Tegra::Shader::TextureType::Texture1D: {
                    std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    coord = "float coords = " + x + ';';
                    break;
                }
                case Tegra::Shader::TextureType::Texture2D: {
                    std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(texture_type));
                    UNREACHABLE();

                    // Fallback to interpreting as a 2D texture for now
                    std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                }

                const std::string sampler = GetSampler(instr.sampler, texture_type, false);
                // Add an extra scope and declare the texture coords inside to prevent
                // overwriting them in case they are used as outputs of the texs instruction.
                shader.AddLine("{");
                ++shader.scope;
                shader.AddLine(coord);
                const std::string texture = "texture(" + sampler + ", coords)";

                size_t dest_elem{};
                for (size_t elem = 0; elem < 4; ++elem) {
                    if (!instr.tex.IsComponentEnabled(elem)) {
                        // Skip disabled components
                        continue;
                    }
                    regs.SetRegisterToFloat(instr.gpr0, elem, texture, 1, 4, false, dest_elem);
                    ++dest_elem;
                }
                --shader.scope;
                shader.AddLine("}");
                break;
            }
            case OpCode::Id::TEXS: {
                std::string coord;
                Tegra::Shader::TextureType texture_type{instr.texs.GetTextureType()};
                bool is_array{instr.texs.IsArrayTexture()};

                switch (texture_type) {
                case Tegra::Shader::TextureType::Texture2D: {
                    if (is_array) {
                        std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                        std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                        coord = "vec3 coords = vec3(" + x + ", " + y + ", " + index + ");";
                    } else {
                        std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                        std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                        coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    }
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(texture_type));
                    UNREACHABLE();

                    // Fallback to interpreting as a 2D texture for now
                    std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                    is_array = false;
                }
                const std::string sampler = GetSampler(instr.sampler, texture_type, is_array);
                const std::string texture = "texture(" + sampler + ", coords)";
                WriteTexsInstruction(instr, coord, texture);
                break;
            }
            case OpCode::Id::TLDS: {
                ASSERT(instr.tlds.GetTextureType() == Tegra::Shader::TextureType::Texture2D);
                ASSERT(instr.tlds.IsArrayTexture() == false);
                std::string coord;

                switch (instr.tlds.GetTextureType()) {
                case Tegra::Shader::TextureType::Texture2D: {
                    if (instr.tlds.IsArrayTexture()) {
                        LOG_CRITICAL(HW_GPU, "Unhandled 2d array texture");
                        UNREACHABLE();
                    } else {
                        std::string x = regs.GetRegisterAsInteger(instr.gpr8);
                        std::string y = regs.GetRegisterAsInteger(instr.gpr20);
                        coord = "ivec2 coords = ivec2(" + x + ", " + y + ");";
                    }
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(instr.tlds.GetTextureType()));
                    UNREACHABLE();
                }
                const std::string sampler = GetSampler(instr.sampler, instr.tlds.GetTextureType(),
                                                       instr.tlds.IsArrayTexture());
                const std::string texture = "texelFetch(" + sampler + ", coords, 0)";
                WriteTexsInstruction(instr, coord, texture);
                break;
            }
            case OpCode::Id::TLD4: {
                ASSERT(instr.tld4.texture_type == Tegra::Shader::TextureType::Texture2D);
                ASSERT(instr.tld4.array == 0);
                std::string coord;

                switch (instr.tld4.texture_type) {
                case Tegra::Shader::TextureType::Texture2D: {
                    std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(instr.tld4.texture_type.Value()));
                    UNREACHABLE();
                }

                const std::string sampler =
                    GetSampler(instr.sampler, instr.tld4.texture_type, false);
                // Add an extra scope and declare the texture coords inside to prevent
                // overwriting them in case they are used as outputs of the texs instruction.
                shader.AddLine("{");
                ++shader.scope;
                shader.AddLine(coord);
                const std::string texture = "textureGather(" + sampler + ", coords, " +
                                            std::to_string(instr.tld4.component) + ')';

                size_t dest_elem{};
                for (size_t elem = 0; elem < 4; ++elem) {
                    if (!instr.tex.IsComponentEnabled(elem)) {
                        // Skip disabled components
                        continue;
                    }
                    regs.SetRegisterToFloat(instr.gpr0, elem, texture, 1, 4, false, dest_elem);
                    ++dest_elem;
                }
                --shader.scope;
                shader.AddLine("}");
                break;
            }
            case OpCode::Id::TLD4S: {
                const std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);
                const std::string op_b = regs.GetRegisterAsFloat(instr.gpr20);
                // TODO(Subv): Figure out how the sampler type is encoded in the TLD4S instruction.
                const std::string sampler =
                    GetSampler(instr.sampler, Tegra::Shader::TextureType::Texture2D, false);
                const std::string coord = "vec2 coords = vec2(" + op_a + ", " + op_b + ");";
                const std::string texture = "textureGather(" + sampler + ", coords, " +
                                            std::to_string(instr.tld4s.component) + ')';
                WriteTexsInstruction(instr, coord, texture);
                break;
            }
            case OpCode::Id::TXQ: {
                // TODO: the new commits on the texture refactor, change the way samplers work.
                // Sadly, not all texture instructions specify the type of texture their sampler
                // uses. This must be fixed at a later instance.
                const std::string sampler =
                    GetSampler(instr.sampler, Tegra::Shader::TextureType::Texture2D, false);
                switch (instr.txq.query_type) {
                case Tegra::Shader::TextureQueryType::Dimension: {
                    const std::string texture = "textureQueryLevels(" + sampler + ')';
                    regs.SetRegisterToInteger(instr.gpr0, true, 0, texture, 1, 1);
                    break;
                }
                default: {
                    LOG_CRITICAL(HW_GPU, "Unhandled texture query type: {}",
                                 static_cast<u32>(instr.txq.query_type.Value()));
                    UNREACHABLE();
                }
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled memory instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::FloatSetPredicate: {
            std::string op_a = instr.fsetp.neg_a ? "-" : "";
            op_a += regs.GetRegisterAsFloat(instr.gpr8);

            if (instr.fsetp.abs_a) {
                op_a = "abs(" + op_a + ')';
            }

            std::string op_b{};

            if (instr.is_b_imm) {
                if (instr.fsetp.neg_b) {
                    // Only the immediate version of fsetp has a neg_b bit.
                    op_b += '-';
                }
                op_b += '(' + GetImmediate19(instr) + ')';
            } else {
                if (instr.is_b_gpr) {
                    op_b += regs.GetRegisterAsFloat(instr.gpr20);
                } else {
                    op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                            GLSLRegister::Type::Float);
                }
            }

            if (instr.fsetp.abs_b) {
                op_b = "abs(" + op_b + ')';
            }

            // We can't use the constant predicate as destination.
            ASSERT(instr.fsetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

            std::string second_pred =
                GetPredicateCondition(instr.fsetp.pred39, instr.fsetp.neg_pred != 0);

            std::string combiner = GetPredicateCombiner(instr.fsetp.op);

            std::string predicate = GetPredicateComparison(instr.fsetp.cond, op_a, op_b);
            // Set the primary predicate to the result of Predicate OP SecondPredicate
            SetPredicate(instr.fsetp.pred3,
                         '(' + predicate + ") " + combiner + " (" + second_pred + ')');

            if (instr.fsetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
                // if enabled
                SetPredicate(instr.fsetp.pred0,
                             "!(" + predicate + ") " + combiner + " (" + second_pred + ')');
            }
            break;
        }
        case OpCode::Type::IntegerSetPredicate: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8, 0, instr.isetp.is_signed);
            std::string op_b;

            if (instr.is_b_imm) {
                op_b += '(' + std::to_string(instr.alu.GetSignedImm20_20()) + ')';
            } else {
                if (instr.is_b_gpr) {
                    op_b += regs.GetRegisterAsInteger(instr.gpr20, 0, instr.isetp.is_signed);
                } else {
                    op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                            GLSLRegister::Type::Integer);
                }
            }

            // We can't use the constant predicate as destination.
            ASSERT(instr.isetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

            std::string second_pred =
                GetPredicateCondition(instr.isetp.pred39, instr.isetp.neg_pred != 0);

            std::string combiner = GetPredicateCombiner(instr.isetp.op);

            std::string predicate = GetPredicateComparison(instr.isetp.cond, op_a, op_b);
            // Set the primary predicate to the result of Predicate OP SecondPredicate
            SetPredicate(instr.isetp.pred3,
                         '(' + predicate + ") " + combiner + " (" + second_pred + ')');

            if (instr.isetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
                // if enabled
                SetPredicate(instr.isetp.pred0,
                             "!(" + predicate + ") " + combiner + " (" + second_pred + ')');
            }
            break;
        }
        case OpCode::Type::PredicateSetPredicate: {
            std::string op_a =
                GetPredicateCondition(instr.psetp.pred12, instr.psetp.neg_pred12 != 0);
            std::string op_b =
                GetPredicateCondition(instr.psetp.pred29, instr.psetp.neg_pred29 != 0);

            // We can't use the constant predicate as destination.
            ASSERT(instr.psetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

            std::string second_pred =
                GetPredicateCondition(instr.psetp.pred39, instr.psetp.neg_pred39 != 0);

            std::string combiner = GetPredicateCombiner(instr.psetp.op);

            std::string predicate =
                '(' + op_a + ") " + GetPredicateCombiner(instr.psetp.cond) + " (" + op_b + ')';

            // Set the primary predicate to the result of Predicate OP SecondPredicate
            SetPredicate(instr.psetp.pred3,
                         '(' + predicate + ") " + combiner + " (" + second_pred + ')');

            if (instr.psetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
                // if enabled
                SetPredicate(instr.psetp.pred0,
                             "!(" + predicate + ") " + combiner + " (" + second_pred + ')');
            }
            break;
        }
        case OpCode::Type::FloatSet: {
            std::string op_a = instr.fset.neg_a ? "-" : "";
            op_a += regs.GetRegisterAsFloat(instr.gpr8);

            if (instr.fset.abs_a) {
                op_a = "abs(" + op_a + ')';
            }

            std::string op_b = instr.fset.neg_b ? "-" : "";

            if (instr.is_b_imm) {
                std::string imm = GetImmediate19(instr);
                if (instr.fset.neg_imm)
                    op_b += "(-" + imm + ')';
                else
                    op_b += imm;
            } else {
                if (instr.is_b_gpr) {
                    op_b += regs.GetRegisterAsFloat(instr.gpr20);
                } else {
                    op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                            GLSLRegister::Type::Float);
                }
            }

            if (instr.fset.abs_b) {
                op_b = "abs(" + op_b + ')';
            }

            // The fset instruction sets a register to 1.0 or -1 (depending on the bf bit) if the
            // condition is true, and to 0 otherwise.
            std::string second_pred =
                GetPredicateCondition(instr.fset.pred39, instr.fset.neg_pred != 0);

            std::string combiner = GetPredicateCombiner(instr.fset.op);

            std::string predicate = "((" + GetPredicateComparison(instr.fset.cond, op_a, op_b) +
                                    ") " + combiner + " (" + second_pred + "))";

            if (instr.fset.bf) {
                regs.SetRegisterToFloat(instr.gpr0, 0, predicate + " ? 1.0 : 0.0", 1, 1);
            } else {
                regs.SetRegisterToInteger(instr.gpr0, false, 0, predicate + " ? 0xFFFFFFFF : 0", 1,
                                          1);
            }
            break;
        }
        case OpCode::Type::IntegerSet: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8, 0, instr.iset.is_signed);

            std::string op_b;

            if (instr.is_b_imm) {
                op_b = std::to_string(instr.alu.GetSignedImm20_20());
            } else {
                if (instr.is_b_gpr) {
                    op_b = regs.GetRegisterAsInteger(instr.gpr20, 0, instr.iset.is_signed);
                } else {
                    op_b = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                           GLSLRegister::Type::Integer);
                }
            }

            // The iset instruction sets a register to 1.0 or -1 (depending on the bf bit) if the
            // condition is true, and to 0 otherwise.
            std::string second_pred =
                GetPredicateCondition(instr.iset.pred39, instr.iset.neg_pred != 0);

            std::string combiner = GetPredicateCombiner(instr.iset.op);

            std::string predicate = "((" + GetPredicateComparison(instr.iset.cond, op_a, op_b) +
                                    ") " + combiner + " (" + second_pred + "))";

            if (instr.iset.bf) {
                regs.SetRegisterToFloat(instr.gpr0, 0, predicate + " ? 1.0 : 0.0", 1, 1);
            } else {
                regs.SetRegisterToInteger(instr.gpr0, false, 0, predicate + " ? 0xFFFFFFFF : 0", 1,
                                          1);
            }
            break;
        }
        case OpCode::Type::Xmad: {
            ASSERT_MSG(!instr.xmad.sign_a, "Unimplemented");
            ASSERT_MSG(!instr.xmad.sign_b, "Unimplemented");

            std::string op_a{regs.GetRegisterAsInteger(instr.gpr8, 0, instr.xmad.sign_a)};
            std::string op_b;
            std::string op_c;

            // TODO(bunnei): Needs to be fixed once op_a or op_b is signed
            ASSERT_MSG(instr.xmad.sign_a == instr.xmad.sign_b, "Unimplemented");
            const bool is_signed{instr.xmad.sign_a == 1};

            bool is_merge{};
            switch (opcode->GetId()) {
            case OpCode::Id::XMAD_CR: {
                is_merge = instr.xmad.merge_56;
                op_b += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                        instr.xmad.sign_b ? GLSLRegister::Type::Integer
                                                          : GLSLRegister::Type::UnsignedInteger);
                op_c += regs.GetRegisterAsInteger(instr.gpr39, 0, is_signed);
                break;
            }
            case OpCode::Id::XMAD_RR: {
                is_merge = instr.xmad.merge_37;
                op_b += regs.GetRegisterAsInteger(instr.gpr20, 0, instr.xmad.sign_b);
                op_c += regs.GetRegisterAsInteger(instr.gpr39, 0, is_signed);
                break;
            }
            case OpCode::Id::XMAD_RC: {
                op_b += regs.GetRegisterAsInteger(instr.gpr39, 0, instr.xmad.sign_b);
                op_c += regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                        is_signed ? GLSLRegister::Type::Integer
                                                  : GLSLRegister::Type::UnsignedInteger);
                break;
            }
            case OpCode::Id::XMAD_IMM: {
                is_merge = instr.xmad.merge_37;
                op_b += std::to_string(instr.xmad.imm20_16);
                op_c += regs.GetRegisterAsInteger(instr.gpr39, 0, is_signed);
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled XMAD instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }

            // TODO(bunnei): Ensure this is right with signed operands
            if (instr.xmad.high_a) {
                op_a = "((" + op_a + ") >> 16)";
            } else {
                op_a = "((" + op_a + ") & 0xFFFF)";
            }

            std::string src2 = '(' + op_b + ')'; // Preserve original source 2
            if (instr.xmad.high_b) {
                op_b = '(' + src2 + " >> 16)";
            } else {
                op_b = '(' + src2 + " & 0xFFFF)";
            }

            std::string product = '(' + op_a + " * " + op_b + ')';
            if (instr.xmad.product_shift_left) {
                product = '(' + product + " << 16)";
            }

            switch (instr.xmad.mode) {
            case Tegra::Shader::XmadMode::None:
                break;
            case Tegra::Shader::XmadMode::CLo:
                op_c = "((" + op_c + ") & 0xFFFF)";
                break;
            case Tegra::Shader::XmadMode::CHi:
                op_c = "((" + op_c + ") >> 16)";
                break;
            case Tegra::Shader::XmadMode::CBcc:
                op_c = "((" + op_c + ") + (" + src2 + "<< 16))";
                break;
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled XMAD mode: {}",
                             static_cast<u32>(instr.xmad.mode.Value()));
                UNREACHABLE();
            }
            }

            std::string sum{'(' + product + " + " + op_c + ')'};
            if (is_merge) {
                sum = "((" + sum + " & 0xFFFF) | (" + src2 + "<< 16))";
            }

            regs.SetRegisterToInteger(instr.gpr0, is_signed, 0, sum, 1, 1);
            break;
        }
        default: {
            switch (opcode->GetId()) {
            case OpCode::Id::EXIT: {
                if (stage == Maxwell3D::Regs::ShaderStage::Fragment) {
                    EmitFragmentOutputsWrite();
                }

                switch (instr.flow.cond) {
                case Tegra::Shader::FlowCondition::Always:
                    shader.AddLine("return true;");
                    if (instr.pred.pred_index == static_cast<u64>(Pred::UnusedIndex)) {
                        // If this is an unconditional exit then just end processing here,
                        // otherwise we have to account for the possibility of the condition
                        // not being met, so continue processing the next instruction.
                        offset = PROGRAM_END - 1;
                    }
                    break;

                case Tegra::Shader::FlowCondition::Fcsm_Tr:
                    // TODO(bunnei): What is this used for? If we assume this conditon is not
                    // satisifed, dual vertex shaders in Farming Simulator make more sense
                    LOG_CRITICAL(HW_GPU, "Skipping unknown FlowCondition::Fcsm_Tr");
                    break;

                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled flow condition: {}",
                                 static_cast<u32>(instr.flow.cond.Value()));
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::KIL: {
                ASSERT(instr.flow.cond == Tegra::Shader::FlowCondition::Always);

                // Enclose "discard" in a conditional, so that GLSL compilation does not complain
                // about unexecuted instructions that may follow this.
                shader.AddLine("if (true) {");
                ++shader.scope;
                shader.AddLine("discard;");
                --shader.scope;
                shader.AddLine("}");

                break;
            }
            case OpCode::Id::BRA: {
                ASSERT_MSG(instr.bra.constant_buffer == 0,
                           "BRA with constant buffers are not implemented");
                u32 target = offset + instr.bra.GetBranchTarget();
                shader.AddLine("{ jmp_to = " + std::to_string(target) + "u; break; }");
                break;
            }
            case OpCode::Id::IPA: {
                const auto& attribute = instr.attribute.fmt28;
                const auto& reg = instr.gpr0;

                Tegra::Shader::IpaMode input_mode{instr.ipa.interp_mode.Value(),
                                                  instr.ipa.sample_mode.Value()};
                regs.SetRegisterToInputAttibute(reg, attribute.element, attribute.index,
                                                input_mode);

                if (instr.ipa.saturate) {
                    regs.SetRegisterToFloat(reg, 0, regs.GetRegisterAsFloat(reg), 1, 1, true);
                }
                break;
            }
            case OpCode::Id::SSY: {
                // The SSY opcode tells the GPU where to re-converge divergent execution paths, it
                // sets the target of the jump that the SYNC instruction will make. The SSY opcode
                // has a similar structure to the BRA opcode.
                ASSERT_MSG(instr.bra.constant_buffer == 0, "Constant buffer SSY is not supported");

                u32 target = offset + instr.bra.GetBranchTarget();
                EmitPushToSSYStack(target);
                break;
            }
            case OpCode::Id::SYNC: {
                // The SYNC opcode jumps to the address previously set by the SSY opcode
                ASSERT(instr.flow.cond == Tegra::Shader::FlowCondition::Always);
                EmitPopFromSSYStack();
                break;
            }
            case OpCode::Id::DEPBAR: {
                // TODO(Subv): Find out if we actually have to care about this instruction or if
                // the GLSL compiler takes care of that for us.
                LOG_WARNING(HW_GPU, "DEPBAR instruction is stubbed");
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled instruction: {}", opcode->GetName());
                UNREACHABLE();
            }
            }

            break;
        }
        }

        // Close the predicate condition scope.
        if (can_be_predicated && instr.pred.pred_index != static_cast<u64>(Pred::UnusedIndex)) {
            --shader.scope;
            shader.AddLine('}');
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

    void Generate(const std::string& suffix) {
        // Add declarations for all subroutines
        for (const auto& subroutine : subroutines) {
            shader.AddLine("bool " + subroutine.GetName() + "();");
        }
        shader.AddNewLine();

        // Add the main entry point
        shader.AddLine("bool exec_" + suffix + "() {");
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

                // TODO(Subv): Figure out the actual depth of the SSY stack, for now it seems
                // unlikely that shaders will use 20 nested SSYs.
                constexpr u32 SSY_STACK_SIZE = 20;
                shader.AddLine("uint ssy_stack[" + std::to_string(SSY_STACK_SIZE) + "];");
                shader.AddLine("uint ssy_stack_top = 0u;");

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
                        shader.AddLine(" jmp_to = " + std::to_string(compile_end) + "u; break; }");
                        labels.emplace(compile_end);
                    }

                    --shader.scope;
                    shader.AddLine('}');
                }

                shader.AddLine("default: return false;");
                shader.AddLine('}');

                --shader.scope;
                shader.AddLine('}');

                shader.AddLine("return false;");
            }

            --shader.scope;
            shader.AddLine("}\n");

            DEBUG_ASSERT(shader.scope == 0);
        }

        GenerateDeclarations();
    }

    /// Add declarations for registers
    void GenerateDeclarations() {
        regs.GenerateDeclarations(suffix);

        for (const auto& pred : declr_predicates) {
            declarations.AddLine("bool " + pred + " = false;");
        }
        declarations.AddNewLine();
    }

private:
    const std::set<Subroutine>& subroutines;
    const ProgramCode& program_code;
    const u32 main_offset;
    Maxwell3D::Regs::ShaderStage stage;
    const std::string& suffix;

    ShaderWriter shader;
    ShaderWriter declarations;
    GLSLRegisterManager regs{shader, declarations, stage, suffix};

    // Declarations
    std::set<std::string> declr_predicates;
}; // namespace Decompiler

std::string GetCommonDeclarations() {
    return fmt::format("#define MAX_CONSTBUFFER_ELEMENTS {}\n",
                       RasterizerOpenGL::MaxConstbufferSize / sizeof(GLvec4));
}

boost::optional<ProgramResult> DecompileProgram(const ProgramCode& program_code, u32 main_offset,
                                                Maxwell3D::Regs::ShaderStage stage,
                                                const std::string& suffix) {
    try {
        auto subroutines = ControlFlowAnalyzer(program_code, main_offset, suffix).GetSubroutines();
        GLSLGenerator generator(subroutines, program_code, main_offset, stage, suffix);
        return ProgramResult{generator.GetShaderCode(), generator.GetEntries()};
    } catch (const DecompileFail& exception) {
        LOG_ERROR(HW_GPU, "Shader decompilation failed: {}", exception.what());
    }
    return boost::none;
}

} // namespace OpenGL::GLShader::Decompiler
