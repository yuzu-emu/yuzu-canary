// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

#include <fmt/format.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/engines/shader_header.h"
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
constexpr u32 PROGRAM_HEADER_SIZE = sizeof(Tegra::Shader::Header);

constexpr u32 MAX_GEOMETRY_BUFFERS = 6;
constexpr u32 MAX_ATTRIBUTES = 0x100; // Size in vec4s, this value is untested

class DecompileFail : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Translate topology
static std::string GetTopologyName(Tegra::Shader::OutputTopology topology) {
    switch (topology) {
    case Tegra::Shader::OutputTopology::PointList:
        return "points";
    case Tegra::Shader::OutputTopology::LineStrip:
        return "line_strip";
    case Tegra::Shader::OutputTopology::TriangleStrip:
        return "triangle_strip";
    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown output topology {}", static_cast<u32>(topology));
        UNREACHABLE();
        return "points";
    }
}

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
        const auto [iter, inserted] =
            exit_method_map.emplace(std::make_pair(begin, end), ExitMethod::Undetermined);
        ExitMethod& exit_method = iter->second;
        if (!inserted)
            return exit_method;

        for (u32 offset = begin; offset != end && offset != PROGRAM_END; ++offset) {
            const Instruction instr = {program_code[offset]};
            if (const auto opcode = OpCode::Decode(instr)) {
                switch (opcode->get().GetId()) {
                case OpCode::Id::EXIT: {
                    // The EXIT instruction can be predicated, which means that the shader can
                    // conditionally end on this instruction. We have to consider the case where the
                    // condition is not met and check the exit method of that other basic block.
                    using Tegra::Shader::Pred;
                    if (instr.pred.pred_index == static_cast<u64>(Pred::UnusedIndex)) {
                        return exit_method = ExitMethod::AlwaysEnd;
                    } else {
                        const ExitMethod not_met = Scan(offset + 1, end, labels);
                        return exit_method = ParallelExit(ExitMethod::AlwaysEnd, not_met);
                    }
                }
                case OpCode::Id::BRA: {
                    const u32 target = offset + instr.bra.GetBranchTarget();
                    labels.insert(target);
                    const ExitMethod no_jmp = Scan(offset + 1, end, labels);
                    const ExitMethod jmp = Scan(target, end, labels);
                    return exit_method = ParallelExit(no_jmp, jmp);
                }
                case OpCode::Id::SSY:
                case OpCode::Id::PBK: {
                    // The SSY and PBK use a similar encoding as the BRA instruction.
                    ASSERT_MSG(instr.bra.constant_buffer == 0,
                               "Constant buffer branching is not supported");
                    const u32 target = offset + instr.bra.GetBranchTarget();
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
        shader_source.append(static_cast<std::size_t>(scope) * 4, ' ');
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

    GLSLRegister(std::size_t index, const std::string& suffix) : index{index}, suffix{suffix} {}

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
    std::size_t GetIndex() const {
        return index;
    }

private:
    const std::size_t index;
    const std::string& suffix;
};

enum class InternalFlag : u64 {
    ZeroFlag = 0,
    CarryFlag = 1,
    OverflowFlag = 2,
    NaNFlag = 3,
    Amount
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
                        const Maxwell3D::Regs::ShaderStage& stage, const std::string& suffix,
                        const Tegra::Shader::Header& header)
        : shader{shader}, declarations{declarations}, stage{stage}, suffix{suffix}, header{header},
          fixed_pipeline_output_attributes_used{}, local_memory_size{0} {
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
                            bool is_saturated = false, u64 dest_elem = 0, bool precise = false) {

        SetRegister(reg, elem, is_saturated ? "clamp(" + value + ", 0.0, 1.0)" : value,
                    dest_num_components, value_num_components, dest_elem, precise);
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
                              u64 dest_elem = 0, Register::Size size = Register::Size::Word,
                              bool sets_cc = false) {
        ASSERT_MSG(!is_saturated, "Unimplemented");

        const std::string func{is_signed ? "intBitsToFloat" : "uintBitsToFloat"};

        SetRegister(reg, elem, func + '(' + ConvertIntegerSize(value, size) + ')',
                    dest_num_components, value_num_components, dest_elem, false);

        if (sets_cc) {
            const std::string zero_condition = "( " + ConvertIntegerSize(value, size) + " == 0 )";
            SetInternalFlag(InternalFlag::ZeroFlag, zero_condition);
            LOG_WARNING(HW_GPU, "Control Codes Imcomplete.");
        }
    }

    /**
     * Writes code that does a register assignment to a half float value operation.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param value The code representing the value to assign. Type has to be half float.
     * @param merge Half float kind of assignment.
     * @param dest_num_components Number of components in the destination.
     * @param value_num_components Number of components in the value.
     * @param is_saturated Optional, when True, saturates the provided value.
     * @param dest_elem Optional, the destination element to use for the operation.
     */
    void SetRegisterToHalfFloat(const Register& reg, u64 elem, const std::string& value,
                                Tegra::Shader::HalfMerge merge, u64 dest_num_components,
                                u64 value_num_components, bool is_saturated = false,
                                u64 dest_elem = 0) {
        ASSERT_MSG(!is_saturated, "Unimplemented");

        const std::string result = [&]() {
            switch (merge) {
            case Tegra::Shader::HalfMerge::H0_H1:
                return "uintBitsToFloat(packHalf2x16(" + value + "))";
            case Tegra::Shader::HalfMerge::F32:
                // Half float instructions take the first component when doing a float cast.
                return "float(" + value + ".x)";
            case Tegra::Shader::HalfMerge::Mrg_H0:
                // TODO(Rodrigo): I guess Mrg_H0 and Mrg_H1 take their respective component from the
                // pack. I couldn't test this on hardware but it shouldn't really matter since most
                // of the time when a Mrg_* flag is used both components will be mirrored. That
                // being said, it deserves a test.
                return "((" + GetRegisterAsInteger(reg, 0, false) +
                       " & 0xffff0000) | (packHalf2x16(" + value + ") & 0x0000ffff))";
            case Tegra::Shader::HalfMerge::Mrg_H1:
                return "((" + GetRegisterAsInteger(reg, 0, false) +
                       " & 0x0000ffff) | (packHalf2x16(" + value + ") & 0xffff0000))";
            default:
                UNREACHABLE();
                return std::string("0");
            }
        }();

        SetRegister(reg, elem, result, dest_num_components, value_num_components, dest_elem, false);
    }

    /**
     * Writes code that does a register assignment to input attribute operation. Input attributes
     * are stored as floats, so this may require conversion.
     * @param reg The destination register to use.
     * @param elem The element to use for the operation.
     * @param attribute The input attribute to use as the source value.
     * @param input_mode The input mode.
     * @param vertex The register that decides which vertex to read from (used in GS).
     */
    void SetRegisterToInputAttibute(const Register& reg, u64 elem, Attribute::Index attribute,
                                    const Tegra::Shader::IpaMode& input_mode,
                                    std::optional<Register> vertex = {}) {
        const std::string dest = GetRegisterAsFloat(reg);
        const std::string src = GetInputAttribute(attribute, input_mode, vertex) + GetSwizzle(elem);
        shader.AddLine(dest + " = " + src + ';');
    }

    std::string GetLocalMemoryAsFloat(const std::string& index) {
        return "lmem[" + index + ']';
    }

    std::string GetLocalMemoryAsInteger(const std::string& index, bool is_signed = false) {
        const std::string func{is_signed ? "floatToIntBits" : "floatBitsToUint"};
        return func + "(lmem[" + index + "])";
    }

    void SetLocalMemoryAsFloat(const std::string& index, const std::string& value) {
        shader.AddLine("lmem[" + index + "] = " + value + ';');
    }

    void SetLocalMemoryAsInteger(const std::string& index, const std::string& value,
                                 bool is_signed = false) {
        const std::string func{is_signed ? "intBitsToFloat" : "uintBitsToFloat"};
        shader.AddLine("lmem[" + index + "] = " + func + '(' + value + ");");
    }

    std::string GetControlCode(const Tegra::Shader::ControlCode cc) const {
        switch (cc) {
        case Tegra::Shader::ControlCode::NEU:
            return "!(" + GetInternalFlag(InternalFlag::ZeroFlag) + ')';
        default:
            LOG_CRITICAL(HW_GPU, "Unimplemented Control Code {}", static_cast<u32>(cc));
            UNREACHABLE();
            return "false";
        }
    }

    std::string GetInternalFlag(const InternalFlag ii) const {
        const u32 code = static_cast<u32>(ii);
        return "internalFlag_" + std::to_string(code) + suffix;
    }

    void SetInternalFlag(const InternalFlag ii, const std::string& value) const {
        shader.AddLine(GetInternalFlag(ii) + " = " + value + ';');
    }

    /**
     * Writes code that does a output attribute assignment to register operation. Output attributes
     * are stored as floats, so this may require conversion.
     * @param attribute The destination output attribute.
     * @param elem The element to use for the operation.
     * @param val_reg The register to use as the source value.
     * @param buf_reg The register that tells which buffer to write to (used in geometry shaders).
     */
    void SetOutputAttributeToRegister(Attribute::Index attribute, u64 elem, const Register& val_reg,
                                      const Register& buf_reg) {
        const std::string dest = GetOutputAttribute(attribute);
        const std::string src = GetRegisterAsFloat(val_reg);

        if (!dest.empty()) {
            // Can happen with unknown/unimplemented output attributes, in which case we ignore the
            // instruction for now.
            if (stage == Maxwell3D::Regs::ShaderStage::Geometry) {
                // TODO(Rodrigo): nouveau sets some attributes after setting emitting a geometry
                // shader. These instructions use a dirty register as buffer index, to avoid some
                // drivers from complaining about out of boundary writes, guard them.
                const std::string buf_index{"((" + GetRegisterAsInteger(buf_reg) + ") % " +
                                            std::to_string(MAX_GEOMETRY_BUFFERS) + ')'};
                shader.AddLine("amem[" + buf_index + "][" +
                               std::to_string(static_cast<u32>(attribute)) + ']' +
                               GetSwizzle(elem) + " = " + src + ';');
            } else {
                if (attribute == Attribute::Index::PointSize) {
                    fixed_pipeline_output_attributes_used.insert(attribute);
                    shader.AddLine(dest + " = " + src + ';');
                } else {
                    shader.AddLine(dest + GetSwizzle(elem) + " = " + src + ';');
                }
            }
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

        const std::string final_offset = fmt::format("({} + {})", index_str, offset / 4);
        const std::string value = 'c' + std::to_string(cbuf_index) + '[' + final_offset + " / 4][" +
                                  final_offset + " % 4]";

        if (type == GLSLRegister::Type::Float) {
            return value;
        } else if (type == GLSLRegister::Type::Integer) {
            return "floatBitsToInt(" + value + ')';
        } else {
            UNREACHABLE();
        }
    }

    /// Add declarations.
    void GenerateDeclarations(const std::string& suffix) {
        GenerateVertex();
        GenerateRegisters(suffix);
        GenerateLocalMemory();
        GenerateInternalFlags();
        GenerateInputAttrs();
        GenerateOutputAttrs();
        GenerateConstBuffers();
        GenerateSamplers();
        GenerateGeometry();
    }

    /// Returns a list of constant buffer declarations.
    std::vector<ConstBufferEntry> GetConstBuffersDeclarations() const {
        std::vector<ConstBufferEntry> result;
        std::copy_if(declr_const_buffers.begin(), declr_const_buffers.end(),
                     std::back_inserter(result), [](const auto& entry) { return entry.IsUsed(); });
        return result;
    }

    /// Returns a list of samplers used in the shader.
    const std::vector<SamplerEntry>& GetSamplers() const {
        return used_samplers;
    }

    /// Returns the GLSL sampler used for the input shader sampler, and creates a new one if
    /// necessary.
    std::string AccessSampler(const Sampler& sampler, Tegra::Shader::TextureType type,
                              bool is_array, bool is_shadow) {
        const auto offset = static_cast<std::size_t>(sampler.index.Value());

        // If this sampler has already been used, return the existing mapping.
        const auto itr =
            std::find_if(used_samplers.begin(), used_samplers.end(),
                         [&](const SamplerEntry& entry) { return entry.GetOffset() == offset; });

        if (itr != used_samplers.end()) {
            ASSERT(itr->GetType() == type && itr->IsArray() == is_array &&
                   itr->IsShadow() == is_shadow);
            return itr->GetName();
        }

        // Otherwise create a new mapping for this sampler
        const std::size_t next_index = used_samplers.size();
        const SamplerEntry entry{stage, offset, next_index, type, is_array, is_shadow};
        used_samplers.emplace_back(entry);
        return entry.GetName();
    }

    void SetLocalMemory(u64 lmem) {
        local_memory_size = lmem;
    }

private:
    /// Generates declarations for registers.
    void GenerateRegisters(const std::string& suffix) {
        for (const auto& reg : regs) {
            declarations.AddLine(GLSLRegister::GetTypeString() + ' ' + reg.GetPrefixString() +
                                 std::to_string(reg.GetIndex()) + '_' + suffix + " = 0;");
        }
        declarations.AddNewLine();
    }

    /// Generates declarations for local memory.
    void GenerateLocalMemory() {
        if (local_memory_size > 0) {
            declarations.AddLine("float lmem[" + std::to_string((local_memory_size - 1 + 4) / 4) +
                                 "];");
            declarations.AddNewLine();
        }
    }

    /// Generates declarations for internal flags.
    void GenerateInternalFlags() {
        for (u32 ii = 0; ii < static_cast<u64>(InternalFlag::Amount); ii++) {
            const InternalFlag code = static_cast<InternalFlag>(ii);
            declarations.AddLine("bool " + GetInternalFlag(code) + " = false;");
        }
        declarations.AddNewLine();
    }

    /// Generates declarations for input attributes.
    void GenerateInputAttrs() {
        for (const auto element : declr_input_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            u32 idx =
                static_cast<u32>(element.first) - static_cast<u32>(Attribute::Index::Attribute_0);
            if (stage != Maxwell3D::Regs::ShaderStage::Vertex) {
                // If inputs are varyings, add an offset
                idx += GENERIC_VARYING_START_LOCATION;
            }

            std::string attr{GetInputAttribute(element.first, element.second)};
            if (stage == Maxwell3D::Regs::ShaderStage::Geometry) {
                attr = "gs_" + attr + "[]";
            }
            declarations.AddLine("layout (location = " + std::to_string(idx) + ") " +
                                 GetInputFlags(element.first) + "in vec4 " + attr + ';');
        }

        declarations.AddNewLine();
    }

    /// Generates declarations for output attributes.
    void GenerateOutputAttrs() {
        for (const auto& index : declr_output_attribute) {
            // TODO(bunnei): Use proper number of elements for these
            const u32 idx = static_cast<u32>(index) -
                            static_cast<u32>(Attribute::Index::Attribute_0) +
                            GENERIC_VARYING_START_LOCATION;
            declarations.AddLine("layout (location = " + std::to_string(idx) + ") out vec4 " +
                                 GetOutputAttribute(index) + ';');
        }
        declarations.AddNewLine();
    }

    /// Generates declarations for constant buffers.
    void GenerateConstBuffers() {
        for (const auto& entry : GetConstBuffersDeclarations()) {
            declarations.AddLine("layout (std140) uniform " + entry.GetName());
            declarations.AddLine('{');
            declarations.AddLine("    vec4 c" + std::to_string(entry.GetIndex()) +
                                 "[MAX_CONSTBUFFER_ELEMENTS];");
            declarations.AddLine("};");
            declarations.AddNewLine();
        }
        declarations.AddNewLine();
    }

    /// Generates declarations for samplers.
    void GenerateSamplers() {
        const auto& samplers = GetSamplers();
        for (const auto& sampler : samplers) {
            declarations.AddLine("uniform " + sampler.GetTypeString() + ' ' + sampler.GetName() +
                                 ';');
        }
        declarations.AddNewLine();
    }

    /// Generates declarations used for geometry shaders.
    void GenerateGeometry() {
        if (stage != Maxwell3D::Regs::ShaderStage::Geometry)
            return;

        declarations.AddLine(
            "layout (" + GetTopologyName(header.common3.output_topology) +
            ", max_vertices = " + std::to_string(header.common4.max_output_vertices) + ") out;");
        declarations.AddNewLine();

        declarations.AddLine("vec4 amem[" + std::to_string(MAX_GEOMETRY_BUFFERS) + "][" +
                             std::to_string(MAX_ATTRIBUTES) + "];");
        declarations.AddNewLine();

        constexpr char buffer[] = "amem[output_buffer]";
        declarations.AddLine("void emit_vertex(uint output_buffer) {");
        ++declarations.scope;
        for (const auto element : declr_output_attribute) {
            declarations.AddLine(GetOutputAttribute(element) + " = " + buffer + '[' +
                                 std::to_string(static_cast<u32>(element)) + "];");
        }

        declarations.AddLine("position = " + std::string(buffer) + '[' +
                             std::to_string(static_cast<u32>(Attribute::Index::Position)) + "];");

        // If a geometry shader is attached, it will always flip (it's the last stage before
        // fragment). For more info about flipping, refer to gl_shader_gen.cpp.
        declarations.AddLine("position.xy *= viewport_flip.xy;");
        declarations.AddLine("gl_Position = position;");
        declarations.AddLine("position.w = 1.0;");
        declarations.AddLine("EmitVertex();");
        --declarations.scope;
        declarations.AddLine('}');
        declarations.AddNewLine();
    }

    void GenerateVertex() {
        if (stage != Maxwell3D::Regs::ShaderStage::Vertex)
            return;
        declarations.AddLine("out gl_PerVertex {");
        ++declarations.scope;
        declarations.AddLine("vec4 gl_Position;");
        for (auto& o : fixed_pipeline_output_attributes_used) {
            if (o == Attribute::Index::PointSize)
                declarations.AddLine("float gl_PointSize;");
        }
        --declarations.scope;
        declarations.AddLine("};");
    }

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
                     u64 dest_num_components, u64 value_num_components, u64 dest_elem,
                     bool precise) {
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

        if (precise && stage != Maxwell3D::Regs::ShaderStage::Fragment) {
            shader.AddLine('{');
            ++shader.scope;
            // This avoids optimizations of constant propagation and keeps the code as the original
            // Sadly using the precise keyword causes "linking" errors on fragment shaders.
            shader.AddLine("precise float tmp = " + src + ';');
            shader.AddLine(dest + " = tmp;");
            --shader.scope;
            shader.AddLine('}');
        } else {
            shader.AddLine(dest + " = " + src + ';');
        }
    }

    /// Build the GLSL register list.
    void BuildRegisterList() {
        regs.reserve(Register::NumRegisters);

        for (std::size_t index = 0; index < Register::NumRegisters; ++index) {
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
                                  const Tegra::Shader::IpaMode& input_mode,
                                  std::optional<Register> vertex = {}) {
        auto GeometryPass = [&](const std::string& name) {
            if (stage == Maxwell3D::Regs::ShaderStage::Geometry && vertex) {
                // TODO(Rodrigo): Guard geometry inputs against out of bound reads. Some games set
                // an 0x80000000 index for those and the shader fails to build. Find out why this
                // happens and what's its intent.
                return "gs_" + name + '[' + GetRegisterAsInteger(*vertex, 0, false) +
                       " % MAX_VERTEX_INPUT]";
            }
            return name;
        };

        switch (attribute) {
        case Attribute::Index::Position:
            if (stage != Maxwell3D::Regs::ShaderStage::Fragment) {
                return GeometryPass("position");
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
                return GeometryPass("input_attribute_" + std::to_string(index));
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

    /// Generates code representing the declaration name of an output attribute register.
    std::string GetOutputAttribute(Attribute::Index attribute) {
        switch (attribute) {
        case Attribute::Index::PointSize:
            return "gl_PointSize";
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
    const Tegra::Shader::Header& header;
    std::unordered_set<Attribute::Index> fixed_pipeline_output_attributes_used;
    u64 local_memory_size;
};

class GLSLGenerator {
public:
    GLSLGenerator(const std::set<Subroutine>& subroutines, const ProgramCode& program_code,
                  u32 main_offset, Maxwell3D::Regs::ShaderStage stage, const std::string& suffix)
        : subroutines(subroutines), program_code(program_code), main_offset(main_offset),
          stage(stage), suffix(suffix) {
        std::memcpy(&header, program_code.data(), sizeof(Tegra::Shader::Header));
        local_memory_size = header.GetLocalMemorySize();
        regs.SetLocalMemory(local_memory_size);
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
    /// Gets the Subroutine object corresponding to the specified address.
    const Subroutine& GetSubroutine(u32 begin, u32 end) const {
        const auto iter = subroutines.find(Subroutine{begin, end, suffix});
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

    /// Generates code representing a vec2 pair unpacked from a half float immediate
    static std::string UnpackHalfImmediate(const Instruction& instr, bool negate) {
        const std::string immediate = GetHalfFloat(std::to_string(instr.half_imm.PackImmediates()));
        if (!negate) {
            return immediate;
        }
        const std::string negate_first = instr.half_imm.first_negate != 0 ? "-" : "";
        const std::string negate_second = instr.half_imm.second_negate != 0 ? "-" : "";
        const std::string negate_vec = "vec2(" + negate_first + "1, " + negate_second + "1)";

        return '(' + immediate + " * " + negate_vec + ')';
    }

    /// Generates code representing a texture sampler.
    std::string GetSampler(const Sampler& sampler, Tegra::Shader::TextureType type, bool is_array,
                           bool is_shadow) {
        return regs.AccessSampler(sampler, type, is_array, is_shadow);
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
     * Transforms the input string GLSL operand into an unpacked half float pair.
     * @note This function returns a float type pair instead of a half float pair. This is because
     * real half floats are not standardized in GLSL but unpackHalf2x16 (which returns a vec2) is.
     * @param operand Input operand. It has to be an unsigned integer.
     * @param type How to unpack the unsigned integer to a half float pair.
     * @param abs Get the absolute value of unpacked half floats.
     * @param neg Get the negative value of unpacked half floats.
     * @returns String corresponding to a half float pair.
     */
    static std::string GetHalfFloat(const std::string& operand,
                                    Tegra::Shader::HalfType type = Tegra::Shader::HalfType::H0_H1,
                                    bool abs = false, bool neg = false) {
        // "vec2" calls emitted in this function are intended to alias components.
        const std::string value = [&]() {
            switch (type) {
            case Tegra::Shader::HalfType::H0_H1:
                return "unpackHalf2x16(" + operand + ')';
            case Tegra::Shader::HalfType::F32:
                return "vec2(uintBitsToFloat(" + operand + "))";
            case Tegra::Shader::HalfType::H0_H0:
            case Tegra::Shader::HalfType::H1_H1: {
                const bool high = type == Tegra::Shader::HalfType::H1_H1;
                const char unpack_index = "xy"[high ? 1 : 0];
                return "vec2(unpackHalf2x16(" + operand + ")." + unpack_index + ')';
            }
            default:
                UNREACHABLE();
                return std::string("vec2(0)");
            }
        }();

        return GetOperandAbsNeg(value, abs, neg);
    }

    /*
     * Returns whether the instruction at the specified offset is a 'sched' instruction.
     * Sched instructions always appear before a sequence of 3 instructions.
     */
    bool IsSchedInstruction(u32 offset) const {
        // sched instructions appear once every 4 instructions.
        static constexpr std::size_t SchedPeriod = 4;
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

        for (std::size_t i = 0; i < shift_amounts.size(); ++i) {
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

        std::size_t written_components = 0;
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

    static u32 TextureCoordinates(Tegra::Shader::TextureType texture_type) {
        switch (texture_type) {
        case Tegra::Shader::TextureType::Texture1D: {
            return 1;
        }
        case Tegra::Shader::TextureType::Texture2D: {
            return 2;
        }
        case Tegra::Shader::TextureType::Texture3D:
        case Tegra::Shader::TextureType::TextureCube: {
            return 3;
        }
        default:
            LOG_CRITICAL(HW_GPU, "Unhandled texture type {}", static_cast<u32>(texture_type));
            UNREACHABLE();
            return 0;
        }
    }

    /*
     * Emits code to push the input target address to the flow address stack, incrementing the stack
     * top.
     */
    void EmitPushToFlowStack(u32 target) {
        shader.AddLine('{');
        ++shader.scope;
        shader.AddLine("flow_stack[flow_stack_top] = " + std::to_string(target) + "u;");
        shader.AddLine("flow_stack_top++;");
        --shader.scope;
        shader.AddLine('}');
    }

    /*
     * Emits code to pop an address from the flow address stack, setting the jump address to the
     * popped address and decrementing the stack top.
     */
    void EmitPopFromFlowStack() {
        shader.AddLine('{');
        ++shader.scope;
        shader.AddLine("flow_stack_top--;");
        shader.AddLine("jmp_to = flow_stack[flow_stack_top];");
        shader.AddLine("break;");
        --shader.scope;
        shader.AddLine('}');
    }

    /// Writes the output values from a fragment shader to the corresponding GLSL output variables.
    void EmitFragmentOutputsWrite() {
        ASSERT(stage == Maxwell3D::Regs::ShaderStage::Fragment);

        ASSERT_MSG(header.ps.omap.sample_mask == 0, "Samplemask write is unimplemented");

        shader.AddLine("if (alpha_test[0] != 0) {");
        ++shader.scope;
        // We start on the register containing the alpha value in the first RT.
        u32 current_reg = 3;
        for (u32 render_target = 0; render_target < Maxwell3D::Regs::NumRenderTargets;
             ++render_target) {
            // TODO(Blinkhawk): verify the behavior of alpha testing on hardware when
            // multiple render targets are used.
            if (header.ps.IsColorComponentOutputEnabled(render_target, 0) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 1) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 2) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 3)) {
                shader.AddLine(fmt::format("if (!AlphaFunc({})) discard;",
                                           regs.GetRegisterAsFloat(current_reg)));
                current_reg += 4;
            }
        }
        --shader.scope;
        shader.AddLine('}');

        // Write the color outputs using the data in the shader registers, disabled
        // rendertargets/components are skipped in the register assignment.
        current_reg = 0;
        for (u32 render_target = 0; render_target < Maxwell3D::Regs::NumRenderTargets;
             ++render_target) {
            // TODO(Subv): Figure out how dual-source blending is configured in the Switch.
            for (u32 component = 0; component < 4; ++component) {
                if (header.ps.IsColorComponentOutputEnabled(render_target, component)) {
                    shader.AddLine(fmt::format("FragColor{}[{}] = {};", render_target, component,
                                               regs.GetRegisterAsFloat(current_reg)));
                    ++current_reg;
                }
            }
        }

        if (header.ps.omap.depth) {
            // The depth output is always 2 registers after the last color output, and current_reg
            // already contains one past the last color register.

            shader.AddLine(
                "gl_FragDepth = " +
                regs.GetRegisterAsFloat(static_cast<Tegra::Shader::Register>(current_reg) + 1) +
                ';');
        }
    }

    /// Unpacks a video instruction operand (e.g. VMAD).
    std::string GetVideoOperand(const std::string& op, bool is_chunk, bool is_signed,
                                Tegra::Shader::VideoType type, u64 byte_height) {
        const std::string value = [&]() {
            if (!is_chunk) {
                const auto offset = static_cast<u32>(byte_height * 8);
                return "((" + op + " >> " + std::to_string(offset) + ") & 0xff)";
            }
            const std::string zero = "0";

            switch (type) {
            case Tegra::Shader::VideoType::Size16_Low:
                return '(' + op + " & 0xffff)";
            case Tegra::Shader::VideoType::Size16_High:
                return '(' + op + " >> 16)";
            case Tegra::Shader::VideoType::Size32:
                // TODO(Rodrigo): From my hardware tests it becomes a bit "mad" when
                // this type is used (1 * 1 + 0 == 0x5b800000). Until a better
                // explanation is found: assert.
                UNIMPLEMENTED();
                return zero;
            case Tegra::Shader::VideoType::Invalid:
                UNREACHABLE_MSG("Invalid instruction encoding");
                return zero;
            default:
                UNREACHABLE();
                return zero;
            }
        }();

        if (is_signed) {
            return "int(" + value + ')';
        }
        return value;
    };

    /// Gets the A operand for a video instruction.
    std::string GetVideoOperandA(Instruction instr) {
        return GetVideoOperand(regs.GetRegisterAsInteger(instr.gpr8, 0, false),
                               instr.video.is_byte_chunk_a != 0, instr.video.signed_a,
                               instr.video.type_a, instr.video.byte_height_a);
    }

    /// Gets the B operand for a video instruction.
    std::string GetVideoOperandB(Instruction instr) {
        if (instr.video.use_register_b) {
            return GetVideoOperand(regs.GetRegisterAsInteger(instr.gpr20, 0, false),
                                   instr.video.is_byte_chunk_b != 0, instr.video.signed_b,
                                   instr.video.type_b, instr.video.byte_height_b);
        } else {
            return '(' +
                   std::to_string(instr.video.signed_b ? static_cast<s16>(instr.alu.GetImm20_16())
                                                       : instr.alu.GetImm20_16()) +
                   ')';
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

        shader.AddLine(
            fmt::format("// {}: {} (0x{:016x})", offset, opcode->get().GetName(), instr.value));

        using Tegra::Shader::Pred;
        ASSERT_MSG(instr.pred.full_pred != Pred::NeverExecute,
                   "NeverExecute predicate not implemented");

        // Some instructions (like SSY) don't have a predicate field, they are always
        // unconditionally executed.
        bool can_be_predicated = OpCode::IsPredicatedInstruction(opcode->get().GetId());

        if (can_be_predicated && instr.pred.pred_index != static_cast<u64>(Pred::UnusedIndex)) {
            shader.AddLine("if (" +
                           GetPredicateCondition(instr.pred.pred_index, instr.negate_pred != 0) +
                           ')');
            shader.AddLine('{');
            ++shader.scope;
        }

        switch (opcode->get().GetType()) {
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

            switch (opcode->get().GetId()) {
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
                                        instr.alu.saturate_d, 0, true);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "FMUL Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::FADD_C:
            case OpCode::Id::FADD_R:
            case OpCode::Id::FADD_IMM: {
                op_a = GetOperandAbsNeg(op_a, instr.alu.abs_a, instr.alu.negate_a);
                op_b = GetOperandAbsNeg(op_b, instr.alu.abs_b, instr.alu.negate_b);

                regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " + " + op_b, 1, 1,
                                        instr.alu.saturate_d, 0, true);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "FADD Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::MUFU: {
                op_a = GetOperandAbsNeg(op_a, instr.alu.abs_a, instr.alu.negate_a);
                switch (instr.sub_op) {
                case SubOp::Cos:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "cos(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Sin:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "sin(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Ex2:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "exp2(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Lg2:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "log2(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Rcp:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "1.0 / " + op_a, 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Rsq:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "inversesqrt(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
                    break;
                case SubOp::Sqrt:
                    regs.SetRegisterToFloat(instr.gpr0, 0, "sqrt(" + op_a + ')', 1, 1,
                                            instr.alu.saturate_d, 0, true);
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
                                        1, 1, false, 0, true);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "FMNMX Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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
                LOG_CRITICAL(HW_GPU, "Unhandled arithmetic instruction: {}",
                             opcode->get().GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::ArithmeticImmediate: {
            switch (opcode->get().GetId()) {
            case OpCode::Id::MOV32_IMM: {
                regs.SetRegisterToFloat(instr.gpr0, 0, GetImmediate32(instr), 1, 1);
                break;
            }
            case OpCode::Id::FMUL32_IMM: {
                regs.SetRegisterToFloat(instr.gpr0, 0,
                                        regs.GetRegisterAsFloat(instr.gpr8) + " * " +
                                            GetImmediate32(instr),
                                        1, 1, instr.fmul32.saturate, 0, true);
                if (instr.op_32.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "FMUL32 Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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

                regs.SetRegisterToFloat(instr.gpr0, 0, op_a + " + " + op_b, 1, 1, false, 0, true);
                if (instr.op_32.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "FADD32 Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            }
            break;
        }
        case OpCode::Type::Bfe: {
            ASSERT_MSG(!instr.bfe.negate_b, "Unimplemented");

            std::string op_a = instr.bfe.negate_a ? "-" : "";
            op_a += regs.GetRegisterAsInteger(instr.gpr8);

            switch (opcode->get().GetId()) {
            case OpCode::Id::BFE_IMM: {
                std::string inner_shift =
                    '(' + op_a + " << " + std::to_string(instr.bfe.GetLeftShiftValue()) + ')';
                std::string outer_shift =
                    '(' + inner_shift + " >> " +
                    std::to_string(instr.bfe.GetLeftShiftValue() + instr.bfe.shift_position) + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, outer_shift, 1, 1);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "BFE Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled BFE instruction: {}", opcode->get().GetName());
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

            switch (opcode->get().GetId()) {
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
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "SHR Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::SHL_C:
            case OpCode::Id::SHL_R:
            case OpCode::Id::SHL_IMM:
                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " << " + op_b, 1, 1);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "SHL Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled shift instruction: {}", opcode->get().GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::ArithmeticIntegerImmediate: {
            std::string op_a = regs.GetRegisterAsInteger(instr.gpr8);
            std::string op_b = std::to_string(instr.alu.imm20_32.Value());

            switch (opcode->get().GetId()) {
            case OpCode::Id::IADD32I:
                if (instr.iadd32i.negate_a)
                    op_a = "-(" + op_a + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " + " + op_b, 1, 1,
                                          instr.iadd32i.saturate != 0);
                if (instr.op_32.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "IADD32 Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            case OpCode::Id::LOP32I: {
                if (instr.alu.lop32i.invert_a)
                    op_a = "~(" + op_a + ')';

                if (instr.alu.lop32i.invert_b)
                    op_b = "~(" + op_b + ')';

                WriteLogicOperation(instr.gpr0, instr.alu.lop32i.operation, op_a, op_b,
                                    Tegra::Shader::PredicateResultMode::None,
                                    Tegra::Shader::Pred::UnusedIndex);
                if (instr.op_32.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "LOP32I Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled ArithmeticIntegerImmediate instruction: {}",
                             opcode->get().GetName());
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

            switch (opcode->get().GetId()) {
            case OpCode::Id::IADD_C:
            case OpCode::Id::IADD_R:
            case OpCode::Id::IADD_IMM: {
                if (instr.alu_integer.negate_a)
                    op_a = "-(" + op_a + ')';

                if (instr.alu_integer.negate_b)
                    op_b = "-(" + op_b + ')';

                regs.SetRegisterToInteger(instr.gpr0, true, 0, op_a + " + " + op_b, 1, 1,
                                          instr.alu.saturate_d);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "IADD Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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

                if (opcode->get().GetId() == OpCode::Id::IADD3_R) {
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
                if (opcode->get().GetId() == OpCode::Id::IADD3_R) {
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

                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "IADD3 Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::ISCADD_C:
            case OpCode::Id::ISCADD_R:
            case OpCode::Id::ISCADD_IMM: {
                if (instr.alu_integer.negate_a)
                    op_a = "-(" + op_a + ')';

                if (instr.alu_integer.negate_b)
                    op_b = "-(" + op_b + ')';

                const std::string shift = std::to_string(instr.alu_integer.shift_amount.Value());

                regs.SetRegisterToInteger(instr.gpr0, true, 0,
                                          "((" + op_a + " << " + shift + ") + " + op_b + ')', 1, 1);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "ISCADD Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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
                const std::string condition =
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
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "LOP Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::LOP3_C:
            case OpCode::Id::LOP3_R:
            case OpCode::Id::LOP3_IMM: {
                const std::string op_c = regs.GetRegisterAsInteger(instr.gpr39);
                std::string lut;

                if (opcode->get().GetId() == OpCode::Id::LOP3_R) {
                    lut = '(' + std::to_string(instr.alu.lop3.GetImmLut28()) + ')';
                } else {
                    lut = '(' + std::to_string(instr.alu.lop3.GetImmLut48()) + ')';
                }

                WriteLop3Instruction(instr.gpr0, op_a, op_b, op_c, lut);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "LOP3 Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::IMNMX_C:
            case OpCode::Id::IMNMX_R:
            case OpCode::Id::IMNMX_IMM: {
                ASSERT_MSG(instr.imnmx.exchange == Tegra::Shader::IMinMaxExchange::None,
                           "Unimplemented");
                const std::string condition =
                    GetPredicateCondition(instr.imnmx.pred, instr.imnmx.negate_pred != 0);
                const std::string parameters = op_a + ',' + op_b;
                regs.SetRegisterToInteger(instr.gpr0, instr.imnmx.is_signed, 0,
                                          '(' + condition + ") ? min(" + parameters + ") : max(" +
                                              parameters + ')',
                                          1, 1);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "IMNMX Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            case OpCode::Id::LEA_R2:
            case OpCode::Id::LEA_R1:
            case OpCode::Id::LEA_IMM:
            case OpCode::Id::LEA_RZ:
            case OpCode::Id::LEA_HI: {
                std::string op_c;

                switch (opcode->get().GetId()) {
                case OpCode::Id::LEA_R2: {
                    op_a = regs.GetRegisterAsInteger(instr.gpr20);
                    op_b = regs.GetRegisterAsInteger(instr.gpr39);
                    op_c = std::to_string(instr.lea.r2.entry_a);
                    break;
                }

                case OpCode::Id::LEA_R1: {
                    const bool neg = instr.lea.r1.neg != 0;
                    op_a = regs.GetRegisterAsInteger(instr.gpr8);
                    if (neg)
                        op_a = "-(" + op_a + ')';
                    op_b = regs.GetRegisterAsInteger(instr.gpr20);
                    op_c = std::to_string(instr.lea.r1.entry_a);
                    break;
                }

                case OpCode::Id::LEA_IMM: {
                    const bool neg = instr.lea.imm.neg != 0;
                    op_b = regs.GetRegisterAsInteger(instr.gpr8);
                    if (neg)
                        op_b = "-(" + op_b + ')';
                    op_a = std::to_string(instr.lea.imm.entry_a);
                    op_c = std::to_string(instr.lea.imm.entry_b);
                    break;
                }

                case OpCode::Id::LEA_RZ: {
                    const bool neg = instr.lea.rz.neg != 0;
                    op_b = regs.GetRegisterAsInteger(instr.gpr8);
                    if (neg)
                        op_b = "-(" + op_b + ')';
                    op_a = regs.GetUniform(instr.lea.rz.cb_index, instr.lea.rz.cb_offset,
                                           GLSLRegister::Type::Integer);
                    op_c = std::to_string(instr.lea.rz.entry_a);

                    break;
                }

                case OpCode::Id::LEA_HI:
                default: {
                    op_b = regs.GetRegisterAsInteger(instr.gpr8);
                    op_a = std::to_string(instr.lea.imm.entry_a);
                    op_c = std::to_string(instr.lea.imm.entry_b);
                    LOG_CRITICAL(HW_GPU, "Unhandled LEA subinstruction: {}",
                                 opcode->get().GetName());
                    UNREACHABLE();
                }
                }
                if (instr.lea.pred48 != static_cast<u64>(Pred::UnusedIndex)) {
                    LOG_ERROR(HW_GPU, "Unhandled LEA Predicate");
                    UNREACHABLE();
                }
                const std::string value = '(' + op_a + " + (" + op_b + "*(1 << " + op_c + ")))";
                regs.SetRegisterToInteger(instr.gpr0, true, 0, value, 1, 1);

                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled ArithmeticInteger instruction: {}",
                             opcode->get().GetName());
                UNREACHABLE();
            }
            }

            break;
        }
        case OpCode::Type::ArithmeticHalf: {
            if (opcode->get().GetId() == OpCode::Id::HADD2_C ||
                opcode->get().GetId() == OpCode::Id::HADD2_R) {
                ASSERT_MSG(instr.alu_half.ftz == 0, "Unimplemented");
            }
            const bool negate_a =
                opcode->get().GetId() != OpCode::Id::HMUL2_R && instr.alu_half.negate_a != 0;
            const bool negate_b =
                opcode->get().GetId() != OpCode::Id::HMUL2_C && instr.alu_half.negate_b != 0;

            const std::string op_a =
                GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr8, 0, false), instr.alu_half.type_a,
                             instr.alu_half.abs_a != 0, negate_a);

            std::string op_b;
            switch (opcode->get().GetId()) {
            case OpCode::Id::HADD2_C:
            case OpCode::Id::HMUL2_C:
                op_b = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                       GLSLRegister::Type::UnsignedInteger);
                break;
            case OpCode::Id::HADD2_R:
            case OpCode::Id::HMUL2_R:
                op_b = regs.GetRegisterAsInteger(instr.gpr20, 0, false);
                break;
            default:
                UNREACHABLE();
                op_b = "0";
                break;
            }
            op_b = GetHalfFloat(op_b, instr.alu_half.type_b, instr.alu_half.abs_b != 0, negate_b);

            const std::string result = [&]() {
                switch (opcode->get().GetId()) {
                case OpCode::Id::HADD2_C:
                case OpCode::Id::HADD2_R:
                    return '(' + op_a + " + " + op_b + ')';
                case OpCode::Id::HMUL2_C:
                case OpCode::Id::HMUL2_R:
                    return '(' + op_a + " * " + op_b + ')';
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled half float instruction: {}",
                                 opcode->get().GetName());
                    UNREACHABLE();
                    return std::string("0");
                }
            }();

            regs.SetRegisterToHalfFloat(instr.gpr0, 0, result, instr.alu_half.merge, 1, 1,
                                        instr.alu_half.saturate != 0);
            break;
        }
        case OpCode::Type::ArithmeticHalfImmediate: {
            if (opcode->get().GetId() == OpCode::Id::HADD2_IMM) {
                ASSERT_MSG(instr.alu_half_imm.ftz == 0, "Unimplemented");
            } else {
                ASSERT_MSG(instr.alu_half_imm.precision == Tegra::Shader::HalfPrecision::None,
                           "Unimplemented");
            }

            const std::string op_a = GetHalfFloat(
                regs.GetRegisterAsInteger(instr.gpr8, 0, false), instr.alu_half_imm.type_a,
                instr.alu_half_imm.abs_a != 0, instr.alu_half_imm.negate_a != 0);

            const std::string op_b = UnpackHalfImmediate(instr, true);

            const std::string result = [&]() {
                switch (opcode->get().GetId()) {
                case OpCode::Id::HADD2_IMM:
                    return op_a + " + " + op_b;
                case OpCode::Id::HMUL2_IMM:
                    return op_a + " * " + op_b;
                default:
                    UNREACHABLE();
                    return std::string("0");
                }
            }();

            regs.SetRegisterToHalfFloat(instr.gpr0, 0, result, instr.alu_half_imm.merge, 1, 1,
                                        instr.alu_half_imm.saturate != 0);
            break;
        }
        case OpCode::Type::Ffma: {
            const std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);
            std::string op_b = instr.ffma.negate_b ? "-" : "";
            std::string op_c = instr.ffma.negate_c ? "-" : "";

            ASSERT_MSG(instr.ffma.cc == 0, "FFMA cc not implemented");
            ASSERT_MSG(instr.ffma.tab5980_0 == 1, "FFMA tab5980_0({}) not implemented",
                       instr.ffma.tab5980_0.Value()); // Seems to be 1 by default based on SMO
            ASSERT_MSG(instr.ffma.tab5980_1 == 0, "FFMA tab5980_1({}) not implemented",
                       instr.ffma.tab5980_1.Value());

            switch (opcode->get().GetId()) {
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
                LOG_CRITICAL(HW_GPU, "Unhandled FFMA instruction: {}", opcode->get().GetName());
                UNREACHABLE();
            }
            }

            regs.SetRegisterToFloat(instr.gpr0, 0, "fma(" + op_a + ", " + op_b + ", " + op_c + ')',
                                    1, 1, instr.alu.saturate_d, 0, true);
            if (instr.generates_cc) {
                LOG_CRITICAL(HW_GPU, "FFMA Generates an unhandled Control Code");
                UNREACHABLE();
            }

            break;
        }
        case OpCode::Type::Hfma2: {
            if (opcode->get().GetId() == OpCode::Id::HFMA2_RR) {
                ASSERT_MSG(instr.hfma2.rr.precision == Tegra::Shader::HalfPrecision::None,
                           "Unimplemented");
            } else {
                ASSERT_MSG(instr.hfma2.precision == Tegra::Shader::HalfPrecision::None,
                           "Unimplemented");
            }
            const bool saturate = opcode->get().GetId() == OpCode::Id::HFMA2_RR
                                      ? instr.hfma2.rr.saturate != 0
                                      : instr.hfma2.saturate != 0;

            const std::string op_a =
                GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr8, 0, false), instr.hfma2.type_a);
            std::string op_b, op_c;

            switch (opcode->get().GetId()) {
            case OpCode::Id::HFMA2_CR:
                op_b = GetHalfFloat(regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                                    GLSLRegister::Type::UnsignedInteger),
                                    instr.hfma2.type_b, false, instr.hfma2.negate_b);
                op_c = GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr39, 0, false),
                                    instr.hfma2.type_reg39, false, instr.hfma2.negate_c);
                break;
            case OpCode::Id::HFMA2_RC:
                op_b = GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr39, 0, false),
                                    instr.hfma2.type_reg39, false, instr.hfma2.negate_b);
                op_c = GetHalfFloat(regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                                    GLSLRegister::Type::UnsignedInteger),
                                    instr.hfma2.type_b, false, instr.hfma2.negate_c);
                break;
            case OpCode::Id::HFMA2_RR:
                op_b = GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr20, 0, false),
                                    instr.hfma2.type_b, false, instr.hfma2.negate_b);
                op_c = GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr39, 0, false),
                                    instr.hfma2.rr.type_c, false, instr.hfma2.rr.negate_c);
                break;
            case OpCode::Id::HFMA2_IMM_R:
                op_b = UnpackHalfImmediate(instr, true);
                op_c = GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr39, 0, false),
                                    instr.hfma2.type_reg39, false, instr.hfma2.negate_c);
                break;
            default:
                UNREACHABLE();
                op_c = op_b = "vec2(0)";
                break;
            }

            const std::string result = '(' + op_a + " * " + op_b + " + " + op_c + ')';

            regs.SetRegisterToHalfFloat(instr.gpr0, 0, result, instr.hfma2.merge, 1, 1, saturate);
            break;
        }
        case OpCode::Type::Conversion: {
            switch (opcode->get().GetId()) {
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
                                          1, instr.alu.saturate_d, 0, instr.conversion.dest_size,
                                          instr.generates_cc.Value() != 0);
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

                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "I2F Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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

                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "F2F Generates an unhandled Control Code");
                    UNREACHABLE();
                }
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
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "F2I Generates an unhandled Control Code");
                    UNREACHABLE();
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled conversion instruction: {}",
                             opcode->get().GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::Memory: {
            switch (opcode->get().GetId()) {
            case OpCode::Id::LD_A: {
                // Note: Shouldn't this be interp mode flat? As in no interpolation made.
                ASSERT_MSG(instr.gpr8.Value() == Register::ZeroIndex,
                           "Indirect attribute loads are not supported");
                ASSERT_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) == 0,
                           "Unaligned attribute loads are not supported");

                Tegra::Shader::IpaMode input_mode{Tegra::Shader::IpaInterpMode::Perspective,
                                                  Tegra::Shader::IpaSampleMode::Default};

                u64 next_element = instr.attribute.fmt20.element;
                u64 next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

                const auto LoadNextElement = [&](u32 reg_offset) {
                    regs.SetRegisterToInputAttibute(instr.gpr0.Value() + reg_offset, next_element,
                                                    static_cast<Attribute::Index>(next_index),
                                                    input_mode, instr.gpr39.Value());

                    // Load the next attribute element into the following register. If the element
                    // to load goes beyond the vec4 size, load the first element of the next
                    // attribute.
                    next_element = (next_element + 1) % 4;
                    next_index = next_index + (next_element == 0 ? 1 : 0);
                };

                const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
                for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
                    LoadNextElement(reg_offset);
                }
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

                const std::string op_a =
                    regs.GetUniformIndirect(instr.cbuf36.index, instr.cbuf36.offset + 0, "index",
                                            GLSLRegister::Type::Float);

                switch (instr.ld_c.type.Value()) {
                case Tegra::Shader::UniformType::Single:
                    regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1);
                    break;

                case Tegra::Shader::UniformType::Double: {
                    const std::string op_b =
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
            case OpCode::Id::LD_L: {
                // Add an extra scope and declare the index register inside to prevent
                // overwriting it in case it is used as an output of the LD instruction.
                shader.AddLine('{');
                ++shader.scope;

                std::string op = '(' + regs.GetRegisterAsInteger(instr.gpr8, 0, false) + " + " +
                                 std::to_string(instr.smem_imm.Value()) + ')';

                shader.AddLine("uint index = (" + op + " / 4);");

                const std::string op_a = regs.GetLocalMemoryAsFloat("index");

                if (instr.ld_l.unknown != 1) {
                    LOG_CRITICAL(HW_GPU, "LD_L Unhandled mode: {}",
                                 static_cast<unsigned>(instr.ld_l.unknown.Value()));
                    UNREACHABLE();
                }

                switch (instr.ldst_sl.type.Value()) {
                case Tegra::Shader::StoreType::Bytes32:
                    regs.SetRegisterToFloat(instr.gpr0, 0, op_a, 1, 1);
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "LD_L Unhandled type: {}",
                                 static_cast<unsigned>(instr.ldst_sl.type.Value()));
                    UNREACHABLE();
                }

                --shader.scope;
                shader.AddLine('}');
                break;
            }
            case OpCode::Id::ST_A: {
                ASSERT_MSG(instr.gpr8.Value() == Register::ZeroIndex,
                           "Indirect attribute loads are not supported");
                ASSERT_MSG((instr.attribute.fmt20.immediate.Value() % sizeof(u32)) == 0,
                           "Unaligned attribute loads are not supported");

                u64 next_element = instr.attribute.fmt20.element;
                u64 next_index = static_cast<u64>(instr.attribute.fmt20.index.Value());

                const auto StoreNextElement = [&](u32 reg_offset) {
                    regs.SetOutputAttributeToRegister(static_cast<Attribute::Index>(next_index),
                                                      next_element, instr.gpr0.Value() + reg_offset,
                                                      instr.gpr39.Value());

                    // Load the next attribute element into the following register. If the element
                    // to load goes beyond the vec4 size, load the first element of the next
                    // attribute.
                    next_element = (next_element + 1) % 4;
                    next_index = next_index + (next_element == 0 ? 1 : 0);
                };

                const u32 num_words = static_cast<u32>(instr.attribute.fmt20.size.Value()) + 1;
                for (u32 reg_offset = 0; reg_offset < num_words; ++reg_offset) {
                    StoreNextElement(reg_offset);
                }

                break;
            }
            case OpCode::Id::ST_L: {
                // Add an extra scope and declare the index register inside to prevent
                // overwriting it in case it is used as an output of the LD instruction.
                shader.AddLine('{');
                ++shader.scope;

                std::string op = '(' + regs.GetRegisterAsInteger(instr.gpr8, 0, false) + " + " +
                                 std::to_string(instr.smem_imm.Value()) + ')';

                shader.AddLine("uint index = (" + op + " / 4);");

                if (instr.st_l.unknown != 0) {
                    LOG_CRITICAL(HW_GPU, "ST_L Unhandled mode: {}",
                                 static_cast<unsigned>(instr.st_l.unknown.Value()));
                    UNREACHABLE();
                }

                switch (instr.ldst_sl.type.Value()) {
                case Tegra::Shader::StoreType::Bytes32:
                    regs.SetLocalMemoryAsFloat("index", regs.GetRegisterAsFloat(instr.gpr0));
                    break;
                default:
                    LOG_CRITICAL(HW_GPU, "ST_L Unhandled type: {}",
                                 static_cast<unsigned>(instr.ldst_sl.type.Value()));
                    UNREACHABLE();
                }

                --shader.scope;
                shader.AddLine('}');
                break;
            }
            case OpCode::Id::TEX: {
                Tegra::Shader::TextureType texture_type{instr.tex.texture_type};
                std::string coord;
                const bool is_array = instr.tex.array != 0;

                ASSERT_MSG(!instr.tex.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");
                ASSERT_MSG(!instr.tex.UsesMiscMode(Tegra::Shader::TextureMiscMode::AOFFI),
                           "AOFFI is not implemented");

                const bool depth_compare =
                    instr.tex.UsesMiscMode(Tegra::Shader::TextureMiscMode::DC);
                u32 num_coordinates = TextureCoordinates(texture_type);
                if (depth_compare)
                    num_coordinates += 1;

                switch (num_coordinates) {
                case 1: {
                    if (is_array) {
                        const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        coord = "vec2 coords = vec2(" + x + ", " + index + ");";
                    } else {
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                        coord = "float coords = " + x + ';';
                    }
                    break;
                }
                case 2: {
                    if (is_array) {
                        const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 2);
                        coord = "vec3 coords = vec3(" + x + ", " + y + ", " + index + ");";
                    } else {
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    }
                    break;
                }
                case 3: {
                    if (depth_compare) {
                        if (is_array) {
                            const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                            const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                            const std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                            const std::string z = regs.GetRegisterAsFloat(instr.gpr20.Value() + 1);
                            coord = "vec4 coords = vec4(" + x + ", " + y + ", " + z + ", " + index +
                                    ");";
                        } else {
                            const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                            const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                            const std::string z = regs.GetRegisterAsFloat(instr.gpr20);
                            coord = "vec3 coords = vec3(" + x + ", " + y + ", " + z + ");";
                        }
                    } else {
                        if (is_array) {
                            const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                            const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                            const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 2);
                            const std::string z = regs.GetRegisterAsFloat(instr.gpr8.Value() + 3);
                            coord = "vec4 coords = vec4(" + x + ", " + y + ", " + z + ", " + index +
                                    ");";
                        } else {
                            const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                            const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                            const std::string z = regs.GetRegisterAsFloat(instr.gpr8.Value() + 2);
                            coord = "vec3 coords = vec3(" + x + ", " + y + ", " + z + ");";
                        }
                    }
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled coordinates number {}",
                                 static_cast<u32>(num_coordinates));
                    UNREACHABLE();

                    // Fallback to interpreting as a 2D texture for now
                    const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                }
                // TODO: make sure coordinates are always indexed to gpr8 and gpr20 is always bias
                // or lod.
                std::string op_c;

                const std::string sampler =
                    GetSampler(instr.sampler, texture_type, is_array, depth_compare);
                // Add an extra scope and declare the texture coords inside to prevent
                // overwriting them in case they are used as outputs of the texs instruction.

                shader.AddLine("{");
                ++shader.scope;
                shader.AddLine(coord);
                std::string texture;

                switch (instr.tex.GetTextureProcessMode()) {
                case Tegra::Shader::TextureProcessMode::None: {
                    texture = "texture(" + sampler + ", coords)";
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LZ: {
                    texture = "textureLod(" + sampler + ", coords, 0.0)";
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LB:
                case Tegra::Shader::TextureProcessMode::LBA: {
                    if (depth_compare) {
                        if (is_array)
                            op_c = regs.GetRegisterAsFloat(instr.gpr20.Value() + 2);
                        else
                            op_c = regs.GetRegisterAsFloat(instr.gpr20.Value() + 1);
                    } else {
                        op_c = regs.GetRegisterAsFloat(instr.gpr20);
                    }
                    // TODO: Figure if A suffix changes the equation at all.
                    texture = "texture(" + sampler + ", coords, " + op_c + ')';
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LL:
                case Tegra::Shader::TextureProcessMode::LLA: {
                    if (num_coordinates <= 2) {
                        op_c = regs.GetRegisterAsFloat(instr.gpr20);
                    } else {
                        op_c = regs.GetRegisterAsFloat(instr.gpr20.Value() + 1);
                    }
                    // TODO: Figure if A suffix changes the equation at all.
                    texture = "textureLod(" + sampler + ", coords, " + op_c + ')';
                    break;
                }
                default: {
                    texture = "texture(" + sampler + ", coords)";
                    LOG_CRITICAL(HW_GPU, "Unhandled texture process mode {}",
                                 static_cast<u32>(instr.tex.GetTextureProcessMode()));
                    UNREACHABLE();
                }
                }
                if (!depth_compare) {
                    std::size_t dest_elem{};
                    for (std::size_t elem = 0; elem < 4; ++elem) {
                        if (!instr.tex.IsComponentEnabled(elem)) {
                            // Skip disabled components
                            continue;
                        }
                        regs.SetRegisterToFloat(instr.gpr0, elem, texture, 1, 4, false, dest_elem);
                        ++dest_elem;
                    }
                } else {
                    regs.SetRegisterToFloat(instr.gpr0, 0, texture, 1, 1, false);
                }
                --shader.scope;
                shader.AddLine("}");
                break;
            }
            case OpCode::Id::TEXS: {
                std::string coord;
                Tegra::Shader::TextureType texture_type{instr.texs.GetTextureType()};
                bool is_array{instr.texs.IsArrayTexture()};

                ASSERT_MSG(!instr.texs.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");

                const bool depth_compare =
                    instr.texs.UsesMiscMode(Tegra::Shader::TextureMiscMode::DC);
                u32 num_coordinates = TextureCoordinates(texture_type);
                if (depth_compare)
                    num_coordinates += 1;

                switch (num_coordinates) {
                case 2: {
                    if (is_array) {
                        const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                        coord = "vec3 coords = vec3(" + x + ", " + y + ", " + index + ");";
                    } else {
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                        coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    }
                    break;
                }
                case 3: {
                    if (is_array) {
                        const std::string index = regs.GetRegisterAsInteger(instr.gpr8);
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 2);
                        const std::string z = regs.GetRegisterAsFloat(instr.gpr20);
                        coord =
                            "vec4 coords = vec4(" + x + ", " + y + ", " + z + ", " + index + ");";
                    } else {
                        const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                        const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                        const std::string z = regs.GetRegisterAsFloat(instr.gpr20);
                        coord = "vec3 coords = vec3(" + x + ", " + y + ", " + z + ");";
                    }
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled coordinates number {}",
                                 static_cast<u32>(num_coordinates));
                    UNREACHABLE();

                    // Fallback to interpreting as a 2D texture for now
                    const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr20);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                    is_array = false;
                }
                const std::string sampler =
                    GetSampler(instr.sampler, texture_type, is_array, depth_compare);
                std::string texture;
                switch (instr.texs.GetTextureProcessMode()) {
                case Tegra::Shader::TextureProcessMode::None: {
                    texture = "texture(" + sampler + ", coords)";
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LZ: {
                    if (depth_compare && is_array) {
                        texture = "texture(" + sampler + ", coords)";
                    } else {
                        texture = "textureLod(" + sampler + ", coords, 0.0)";
                    }
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LL: {
                    const std::string op_c = regs.GetRegisterAsFloat(instr.gpr20.Value() + 1);
                    texture = "textureLod(" + sampler + ", coords, " + op_c + ')';
                    break;
                }
                default: {
                    texture = "texture(" + sampler + ", coords)";
                    LOG_CRITICAL(HW_GPU, "Unhandled texture process mode {}",
                                 static_cast<u32>(instr.texs.GetTextureProcessMode()));
                    UNREACHABLE();
                }
                }
                if (!depth_compare) {
                    WriteTexsInstruction(instr, coord, texture);
                } else {
                    WriteTexsInstruction(instr, coord, "vec4(" + texture + ')');
                }
                break;
            }
            case OpCode::Id::TLDS: {
                std::string coord;
                const Tegra::Shader::TextureType texture_type{instr.tlds.GetTextureType()};
                const bool is_array{instr.tlds.IsArrayTexture()};

                ASSERT(texture_type == Tegra::Shader::TextureType::Texture2D);
                ASSERT(is_array == false);

                ASSERT_MSG(!instr.tlds.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");
                ASSERT_MSG(!instr.tlds.UsesMiscMode(Tegra::Shader::TextureMiscMode::AOFFI),
                           "AOFFI is not implemented");
                ASSERT_MSG(!instr.tlds.UsesMiscMode(Tegra::Shader::TextureMiscMode::MZ),
                           "MZ is not implemented");

                u32 op_c_offset = 0;

                switch (texture_type) {
                case Tegra::Shader::TextureType::Texture1D: {
                    const std::string x = regs.GetRegisterAsInteger(instr.gpr8);
                    coord = "int coords = " + x + ';';
                    break;
                }
                case Tegra::Shader::TextureType::Texture2D: {
                    if (is_array) {
                        LOG_CRITICAL(HW_GPU, "Unhandled 2d array texture");
                        UNREACHABLE();
                    } else {
                        const std::string x = regs.GetRegisterAsInteger(instr.gpr8);
                        const std::string y = regs.GetRegisterAsInteger(instr.gpr20);
                        coord = "ivec2 coords = ivec2(" + x + ", " + y + ");";
                        op_c_offset = 1;
                    }
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(texture_type));
                    UNREACHABLE();
                }
                const std::string sampler =
                    GetSampler(instr.sampler, texture_type, is_array, false);
                std::string texture = "texelFetch(" + sampler + ", coords, 0)";
                switch (instr.tlds.GetTextureProcessMode()) {
                case Tegra::Shader::TextureProcessMode::LZ: {
                    texture = "texelFetch(" + sampler + ", coords, 0)";
                    break;
                }
                case Tegra::Shader::TextureProcessMode::LL: {
                    const std::string op_c =
                        regs.GetRegisterAsInteger(instr.gpr20.Value() + op_c_offset);
                    texture = "texelFetch(" + sampler + ", coords, " + op_c + ')';
                    break;
                }
                default: {
                    texture = "texelFetch(" + sampler + ", coords, 0)";
                    LOG_CRITICAL(HW_GPU, "Unhandled texture process mode {}",
                                 static_cast<u32>(instr.tlds.GetTextureProcessMode()));
                    UNREACHABLE();
                }
                }
                WriteTexsInstruction(instr, coord, texture);
                break;
            }
            case OpCode::Id::TLD4: {
                ASSERT(instr.tld4.texture_type == Tegra::Shader::TextureType::Texture2D);
                ASSERT(instr.tld4.array == 0);
                std::string coord;

                ASSERT_MSG(!instr.tld4.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");
                ASSERT_MSG(!instr.tld4.UsesMiscMode(Tegra::Shader::TextureMiscMode::AOFFI),
                           "AOFFI is not implemented");
                ASSERT_MSG(!instr.tld4.UsesMiscMode(Tegra::Shader::TextureMiscMode::NDV),
                           "NDV is not implemented");
                ASSERT_MSG(!instr.tld4.UsesMiscMode(Tegra::Shader::TextureMiscMode::PTP),
                           "PTP is not implemented");
                const bool depth_compare =
                    instr.tld4.UsesMiscMode(Tegra::Shader::TextureMiscMode::DC);
                auto texture_type = instr.tld4.texture_type.Value();
                u32 num_coordinates = TextureCoordinates(texture_type);
                if (depth_compare)
                    num_coordinates += 1;

                switch (num_coordinates) {
                case 2: {
                    const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    break;
                }
                case 3: {
                    const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    const std::string z = regs.GetRegisterAsFloat(instr.gpr8.Value() + 2);
                    coord = "vec3 coords = vec3(" + x + ", " + y + ", " + z + ");";
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled coordinates number {}",
                                 static_cast<u32>(num_coordinates));
                    UNREACHABLE();
                    const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                }

                const std::string sampler =
                    GetSampler(instr.sampler, texture_type, false, depth_compare);
                // Add an extra scope and declare the texture coords inside to prevent
                // overwriting them in case they are used as outputs of the texs instruction.
                shader.AddLine("{");
                ++shader.scope;
                shader.AddLine(coord);
                const std::string texture = "textureGather(" + sampler + ", coords, " +
                                            std::to_string(instr.tld4.component) + ')';
                if (!depth_compare) {
                    std::size_t dest_elem{};
                    for (std::size_t elem = 0; elem < 4; ++elem) {
                        if (!instr.tex.IsComponentEnabled(elem)) {
                            // Skip disabled components
                            continue;
                        }
                        regs.SetRegisterToFloat(instr.gpr0, elem, texture, 1, 4, false, dest_elem);
                        ++dest_elem;
                    }
                } else {
                    regs.SetRegisterToFloat(instr.gpr0, 0, texture, 1, 1, false);
                }
                --shader.scope;
                shader.AddLine("}");
                break;
            }
            case OpCode::Id::TLD4S: {
                ASSERT_MSG(!instr.tld4s.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");
                ASSERT_MSG(!instr.tld4s.UsesMiscMode(Tegra::Shader::TextureMiscMode::AOFFI),
                           "AOFFI is not implemented");

                const bool depth_compare =
                    instr.tld4s.UsesMiscMode(Tegra::Shader::TextureMiscMode::DC);
                const std::string op_a = regs.GetRegisterAsFloat(instr.gpr8);
                const std::string op_b = regs.GetRegisterAsFloat(instr.gpr20);
                // TODO(Subv): Figure out how the sampler type is encoded in the TLD4S instruction.
                const std::string sampler = GetSampler(
                    instr.sampler, Tegra::Shader::TextureType::Texture2D, false, depth_compare);
                std::string coord;
                if (!depth_compare) {
                    coord = "vec2 coords = vec2(" + op_a + ", " + op_b + ");";
                } else {
                    // Note: TLD4S coordinate encoding works just like TEXS's
                    const std::string op_c = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec3 coords = vec3(" + op_a + ", " + op_c + ", " + op_b + ");";
                }
                const std::string texture = "textureGather(" + sampler + ", coords, " +
                                            std::to_string(instr.tld4s.component) + ')';

                if (!depth_compare) {
                    WriteTexsInstruction(instr, coord, texture);
                } else {
                    WriteTexsInstruction(instr, coord, "vec4(" + texture + ')');
                }
                break;
            }
            case OpCode::Id::TXQ: {
                ASSERT_MSG(!instr.txq.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");

                // TODO: the new commits on the texture refactor, change the way samplers work.
                // Sadly, not all texture instructions specify the type of texture their sampler
                // uses. This must be fixed at a later instance.
                const std::string sampler =
                    GetSampler(instr.sampler, Tegra::Shader::TextureType::Texture2D, false, false);
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
            case OpCode::Id::TMML: {
                ASSERT_MSG(!instr.tmml.UsesMiscMode(Tegra::Shader::TextureMiscMode::NODEP),
                           "NODEP is not implemented");
                ASSERT_MSG(!instr.tmml.UsesMiscMode(Tegra::Shader::TextureMiscMode::NDV),
                           "NDV is not implemented");

                const std::string x = regs.GetRegisterAsFloat(instr.gpr8);
                const bool is_array = instr.tmml.array != 0;
                auto texture_type = instr.tmml.texture_type.Value();
                const std::string sampler =
                    GetSampler(instr.sampler, texture_type, is_array, false);

                // TODO: add coordinates for different samplers once other texture types are
                // implemented.
                std::string coord;
                switch (texture_type) {
                case Tegra::Shader::TextureType::Texture1D: {
                    coord = "float coords = " + x + ';';
                    break;
                }
                case Tegra::Shader::TextureType::Texture2D: {
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    break;
                }
                default:
                    LOG_CRITICAL(HW_GPU, "Unhandled texture type {}",
                                 static_cast<u32>(texture_type));
                    UNREACHABLE();

                    // Fallback to interpreting as a 2D texture for now
                    const std::string y = regs.GetRegisterAsFloat(instr.gpr8.Value() + 1);
                    coord = "vec2 coords = vec2(" + x + ", " + y + ");";
                    texture_type = Tegra::Shader::TextureType::Texture2D;
                }
                // Add an extra scope and declare the texture coords inside to prevent
                // overwriting them in case they are used as outputs of the texs instruction.
                shader.AddLine('{');
                ++shader.scope;
                shader.AddLine(coord);
                const std::string texture = "textureQueryLod(" + sampler + ", coords)";
                const std::string tmp = "vec2 tmp = " + texture + "*vec2(256.0, 256.0);";
                shader.AddLine(tmp);

                regs.SetRegisterToInteger(instr.gpr0, true, 0, "int(tmp.y)", 1, 1);
                regs.SetRegisterToInteger(instr.gpr0.Value() + 1, false, 0, "uint(tmp.x)", 1, 1);
                --shader.scope;
                shader.AddLine('}');
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled memory instruction: {}", opcode->get().GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::FloatSetPredicate: {
            const std::string op_a =
                GetOperandAbsNeg(regs.GetRegisterAsFloat(instr.gpr8), instr.fsetp.abs_a != 0,
                                 instr.fsetp.neg_a != 0);

            std::string op_b;

            if (instr.is_b_imm) {
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

            const std::string second_pred =
                GetPredicateCondition(instr.fsetp.pred39, instr.fsetp.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.fsetp.op);

            const std::string predicate = GetPredicateComparison(instr.fsetp.cond, op_a, op_b);
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
            const std::string op_a =
                regs.GetRegisterAsInteger(instr.gpr8, 0, instr.isetp.is_signed);
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

            const std::string second_pred =
                GetPredicateCondition(instr.isetp.pred39, instr.isetp.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.isetp.op);

            const std::string predicate = GetPredicateComparison(instr.isetp.cond, op_a, op_b);
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
        case OpCode::Type::HalfSetPredicate: {
            ASSERT_MSG(instr.hsetp2.ftz == 0, "Unimplemented");

            const std::string op_a =
                GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr8, 0, false), instr.hsetp2.type_a,
                             instr.hsetp2.abs_a, instr.hsetp2.negate_a);

            const std::string op_b = [&]() {
                switch (opcode->get().GetId()) {
                case OpCode::Id::HSETP2_R:
                    return GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr20, 0, false),
                                        instr.hsetp2.type_b, instr.hsetp2.abs_a,
                                        instr.hsetp2.negate_b);
                default:
                    UNREACHABLE();
                    return std::string("vec2(0)");
                }
            }();

            // We can't use the constant predicate as destination.
            ASSERT(instr.hsetp2.pred3 != static_cast<u64>(Pred::UnusedIndex));

            const std::string second_pred =
                GetPredicateCondition(instr.hsetp2.pred39, instr.hsetp2.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.hsetp2.op);

            const std::string component_combiner = instr.hsetp2.h_and ? "&&" : "||";
            const std::string predicate =
                '(' + GetPredicateComparison(instr.hsetp2.cond, op_a + ".x", op_b + ".x") + ' ' +
                component_combiner + ' ' +
                GetPredicateComparison(instr.hsetp2.cond, op_a + ".y", op_b + ".y") + ')';

            // Set the primary predicate to the result of Predicate OP SecondPredicate
            SetPredicate(instr.hsetp2.pred3,
                         '(' + predicate + ") " + combiner + " (" + second_pred + ')');

            if (instr.hsetp2.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
                // if enabled
                SetPredicate(instr.hsetp2.pred0,
                             "!(" + predicate + ") " + combiner + " (" + second_pred + ')');
            }
            break;
        }
        case OpCode::Type::PredicateSetRegister: {
            const std::string op_a =
                GetPredicateCondition(instr.pset.pred12, instr.pset.neg_pred12 != 0);
            const std::string op_b =
                GetPredicateCondition(instr.pset.pred29, instr.pset.neg_pred29 != 0);

            const std::string second_pred =
                GetPredicateCondition(instr.pset.pred39, instr.pset.neg_pred39 != 0);

            const std::string combiner = GetPredicateCombiner(instr.pset.op);

            const std::string predicate =
                '(' + op_a + ") " + GetPredicateCombiner(instr.pset.cond) + " (" + op_b + ')';
            const std::string result = '(' + predicate + ") " + combiner + " (" + second_pred + ')';
            if (instr.pset.bf == 0) {
                const std::string value = '(' + result + ") ? 0xFFFFFFFF : 0";
                regs.SetRegisterToInteger(instr.gpr0, false, 0, value, 1, 1);
            } else {
                const std::string value = '(' + result + ") ? 1.0 : 0.0";
                regs.SetRegisterToFloat(instr.gpr0, 0, value, 1, 1);
            }

            if (instr.generates_cc) {
                LOG_CRITICAL(HW_GPU, "PSET Generates an unhandled Control Code");
                UNREACHABLE();
            }

            break;
        }
        case OpCode::Type::PredicateSetPredicate: {
            switch (opcode->get().GetId()) {
            case OpCode::Id::PSETP: {
                const std::string op_a =
                    GetPredicateCondition(instr.psetp.pred12, instr.psetp.neg_pred12 != 0);
                const std::string op_b =
                    GetPredicateCondition(instr.psetp.pred29, instr.psetp.neg_pred29 != 0);

                // We can't use the constant predicate as destination.
                ASSERT(instr.psetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

                const std::string second_pred =
                    GetPredicateCondition(instr.psetp.pred39, instr.psetp.neg_pred39 != 0);

                const std::string combiner = GetPredicateCombiner(instr.psetp.op);

                const std::string predicate =
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
            case OpCode::Id::CSETP: {
                const std::string pred =
                    GetPredicateCondition(instr.csetp.pred39, instr.csetp.neg_pred39 != 0);
                const std::string combiner = GetPredicateCombiner(instr.csetp.op);
                const std::string control_code = regs.GetControlCode(instr.csetp.cc);
                if (instr.csetp.pred3 != static_cast<u64>(Pred::UnusedIndex)) {
                    SetPredicate(instr.csetp.pred3,
                                 '(' + control_code + ") " + combiner + " (" + pred + ')');
                }
                if (instr.csetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                    SetPredicate(instr.csetp.pred0,
                                 "!(" + control_code + ") " + combiner + " (" + pred + ')');
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled predicate instruction: {}",
                             opcode->get().GetName());
                UNREACHABLE();
            }
            }
            break;
        }
        case OpCode::Type::RegisterSetPredicate: {
            UNIMPLEMENTED_IF(instr.r2p.mode != Tegra::Shader::R2pMode::Pr);

            const std::string apply_mask = [&]() {
                switch (opcode->get().GetId()) {
                case OpCode::Id::R2P_IMM:
                    return std::to_string(instr.r2p.immediate_mask);
                default:
                    UNREACHABLE();
                }
            }();
            const std::string mask = '(' + regs.GetRegisterAsInteger(instr.gpr8, 0, false) +
                                     " >> " + std::to_string(instr.r2p.byte) + ')';

            constexpr u64 programmable_preds = 7;
            for (u64 pred = 0; pred < programmable_preds; ++pred) {
                const auto shift = std::to_string(1 << pred);

                shader.AddLine("if ((" + apply_mask + " & " + shift + ") != 0) {");
                ++shader.scope;

                SetPredicate(pred, '(' + mask + " & " + shift + ") != 0");

                --shader.scope;
                shader.AddLine('}');
            }
            break;
        }
        case OpCode::Type::FloatSet: {
            const std::string op_a = GetOperandAbsNeg(regs.GetRegisterAsFloat(instr.gpr8),
                                                      instr.fset.abs_a != 0, instr.fset.neg_a != 0);

            std::string op_b;

            if (instr.is_b_imm) {
                const std::string imm = GetImmediate19(instr);
                op_b = imm;
            } else {
                if (instr.is_b_gpr) {
                    op_b = regs.GetRegisterAsFloat(instr.gpr20);
                } else {
                    op_b = regs.GetUniform(instr.cbuf34.index, instr.cbuf34.offset,
                                           GLSLRegister::Type::Float);
                }
            }

            op_b = GetOperandAbsNeg(op_b, instr.fset.abs_b != 0, instr.fset.neg_b != 0);

            // The fset instruction sets a register to 1.0 or -1 (depending on the bf bit) if the
            // condition is true, and to 0 otherwise.
            const std::string second_pred =
                GetPredicateCondition(instr.fset.pred39, instr.fset.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.fset.op);

            const std::string predicate = "((" +
                                          GetPredicateComparison(instr.fset.cond, op_a, op_b) +
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
            const std::string op_a = regs.GetRegisterAsInteger(instr.gpr8, 0, instr.iset.is_signed);

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
            const std::string second_pred =
                GetPredicateCondition(instr.iset.pred39, instr.iset.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.iset.op);

            const std::string predicate = "((" +
                                          GetPredicateComparison(instr.iset.cond, op_a, op_b) +
                                          ") " + combiner + " (" + second_pred + "))";

            if (instr.iset.bf) {
                regs.SetRegisterToFloat(instr.gpr0, 0, predicate + " ? 1.0 : 0.0", 1, 1);
            } else {
                regs.SetRegisterToInteger(instr.gpr0, false, 0, predicate + " ? 0xFFFFFFFF : 0", 1,
                                          1);
            }
            break;
        }
        case OpCode::Type::HalfSet: {
            ASSERT_MSG(instr.hset2.ftz == 0, "Unimplemented");

            const std::string op_a =
                GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr8, 0, false), instr.hset2.type_a,
                             instr.hset2.abs_a != 0, instr.hset2.negate_a != 0);

            const std::string op_b = [&]() {
                switch (opcode->get().GetId()) {
                case OpCode::Id::HSET2_R:
                    return GetHalfFloat(regs.GetRegisterAsInteger(instr.gpr20, 0, false),
                                        instr.hset2.type_b, instr.hset2.abs_b != 0,
                                        instr.hset2.negate_b != 0);
                default:
                    UNREACHABLE();
                    return std::string("vec2(0)");
                }
            }();

            const std::string second_pred =
                GetPredicateCondition(instr.hset2.pred39, instr.hset2.neg_pred != 0);

            const std::string combiner = GetPredicateCombiner(instr.hset2.op);

            // HSET2 operates on each half float in the pack.
            std::string result;
            for (int i = 0; i < 2; ++i) {
                const std::string float_value = i == 0 ? "0x00003c00" : "0x3c000000";
                const std::string integer_value = i == 0 ? "0x0000ffff" : "0xffff0000";
                const std::string value = instr.hset2.bf == 1 ? float_value : integer_value;

                const std::string comp = std::string(".") + "xy"[i];
                const std::string predicate =
                    "((" + GetPredicateComparison(instr.hset2.cond, op_a + comp, op_b + comp) +
                    ") " + combiner + " (" + second_pred + "))";

                result += '(' + predicate + " ? " + value + " : 0)";
                if (i == 0) {
                    result += " | ";
                }
            }
            regs.SetRegisterToInteger(instr.gpr0, false, 0, '(' + result + ')', 1, 1);
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
            switch (opcode->get().GetId()) {
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
                LOG_CRITICAL(HW_GPU, "Unhandled XMAD instruction: {}", opcode->get().GetName());
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
            if (instr.generates_cc) {
                LOG_CRITICAL(HW_GPU, "XMAD Generates an unhandled Control Code");
                UNREACHABLE();
            }
            break;
        }
        default: {
            switch (opcode->get().GetId()) {
            case OpCode::Id::EXIT: {
                if (stage == Maxwell3D::Regs::ShaderStage::Fragment) {
                    EmitFragmentOutputsWrite();
                }

                const Tegra::Shader::ControlCode cc = instr.flow_control_code;
                if (cc != Tegra::Shader::ControlCode::T) {
                    LOG_CRITICAL(HW_GPU, "EXIT Control Code used: {}", static_cast<u32>(cc));
                    UNREACHABLE();
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
                const Tegra::Shader::ControlCode cc = instr.flow_control_code;
                if (cc != Tegra::Shader::ControlCode::T) {
                    LOG_CRITICAL(HW_GPU, "KIL Control Code used: {}", static_cast<u32>(cc));
                    UNREACHABLE();
                }
                shader.AddLine("if (true) {");
                ++shader.scope;
                shader.AddLine("discard;");
                --shader.scope;
                shader.AddLine("}");

                break;
            }
            case OpCode::Id::OUT_R: {
                ASSERT(instr.gpr20.Value() == Register::ZeroIndex);
                ASSERT_MSG(stage == Maxwell3D::Regs::ShaderStage::Geometry,
                           "OUT is expected to be used in a geometry shader.");

                if (instr.out.emit) {
                    // gpr0 is used to store the next address. Hardware returns a pointer but
                    // we just return the next index with a cyclic cap.
                    const std::string current{regs.GetRegisterAsInteger(instr.gpr8, 0, false)};
                    const std::string next = "((" + current + " + 1" + ") % " +
                                             std::to_string(MAX_GEOMETRY_BUFFERS) + ')';
                    shader.AddLine("emit_vertex(" + current + ");");
                    regs.SetRegisterToInteger(instr.gpr0, false, 0, next, 1, 1);
                }
                if (instr.out.cut) {
                    shader.AddLine("EndPrimitive();");
                }

                break;
            }
            case OpCode::Id::MOV_SYS: {
                switch (instr.sys20) {
                case Tegra::Shader::SystemVariable::InvocationInfo: {
                    LOG_WARNING(HW_GPU, "MOV_SYS instruction with InvocationInfo is incomplete");
                    regs.SetRegisterToInteger(instr.gpr0, false, 0, "0u", 1, 1);
                    break;
                }
                default: {
                    LOG_CRITICAL(HW_GPU, "Unhandled system move: {}",
                                 static_cast<u32>(instr.sys20.Value()));
                    UNREACHABLE();
                }
                }
                break;
            }
            case OpCode::Id::ISBERD: {
                ASSERT(instr.isberd.o == 0);
                ASSERT(instr.isberd.skew == 0);
                ASSERT(instr.isberd.shift == Tegra::Shader::IsberdShift::None);
                ASSERT(instr.isberd.mode == Tegra::Shader::IsberdMode::None);
                ASSERT_MSG(stage == Maxwell3D::Regs::ShaderStage::Geometry,
                           "ISBERD is expected to be used in a geometry shader.");
                LOG_WARNING(HW_GPU, "ISBERD instruction is incomplete");
                regs.SetRegisterToFloat(instr.gpr0, 0, regs.GetRegisterAsFloat(instr.gpr8), 1, 1);
                break;
            }
            case OpCode::Id::BRA: {
                ASSERT_MSG(instr.bra.constant_buffer == 0,
                           "BRA with constant buffers are not implemented");
                const Tegra::Shader::ControlCode cc = instr.flow_control_code;
                if (cc != Tegra::Shader::ControlCode::T) {
                    LOG_CRITICAL(HW_GPU, "BRA Control Code used: {}", static_cast<u32>(cc));
                    UNREACHABLE();
                }
                const u32 target = offset + instr.bra.GetBranchTarget();
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
                ASSERT_MSG(instr.bra.constant_buffer == 0, "Constant buffer flow is not supported");

                const u32 target = offset + instr.bra.GetBranchTarget();
                EmitPushToFlowStack(target);
                break;
            }
            case OpCode::Id::PBK: {
                // PBK pushes to a stack the address where BRK will jump to. This shares stack with
                // SSY but using SYNC on a PBK address will kill the shader execution. We don't
                // emulate this because it's very unlikely a driver will emit such invalid shader.
                ASSERT_MSG(instr.bra.constant_buffer == 0, "Constant buffer PBK is not supported");

                const u32 target = offset + instr.bra.GetBranchTarget();
                EmitPushToFlowStack(target);
                break;
            }
            case OpCode::Id::SYNC: {
                // The SYNC opcode jumps to the address previously set by the SSY opcode
                const Tegra::Shader::ControlCode cc = instr.flow_control_code;
                if (cc != Tegra::Shader::ControlCode::T) {
                    LOG_CRITICAL(HW_GPU, "SYNC Control Code used: {}", static_cast<u32>(cc));
                    UNREACHABLE();
                }
                EmitPopFromFlowStack();
                break;
            }
            case OpCode::Id::BRK: {
                // The BRK opcode jumps to the address previously set by the PBK opcode
                const Tegra::Shader::ControlCode cc = instr.flow_control_code;
                if (cc != Tegra::Shader::ControlCode::T) {
                    LOG_CRITICAL(HW_GPU, "BRK Control Code used: {}", static_cast<u32>(cc));
                    UNREACHABLE();
                }
                EmitPopFromFlowStack();
                break;
            }
            case OpCode::Id::DEPBAR: {
                // TODO(Subv): Find out if we actually have to care about this instruction or if
                // the GLSL compiler takes care of that for us.
                LOG_WARNING(HW_GPU, "DEPBAR instruction is stubbed");
                break;
            }
            case OpCode::Id::VMAD: {
                const bool result_signed = instr.video.signed_a == 1 || instr.video.signed_b == 1;
                const std::string op_a = GetVideoOperandA(instr);
                const std::string op_b = GetVideoOperandB(instr);
                const std::string op_c = regs.GetRegisterAsInteger(instr.gpr39, 0, result_signed);

                std::string result = '(' + op_a + " * " + op_b + " + " + op_c + ')';

                switch (instr.vmad.shr) {
                case Tegra::Shader::VmadShr::Shr7:
                    result = '(' + result + " >> 7)";
                    break;
                case Tegra::Shader::VmadShr::Shr15:
                    result = '(' + result + " >> 15)";
                    break;
                }

                regs.SetRegisterToInteger(instr.gpr0, result_signed, 1, result, 1, 1,
                                          instr.vmad.saturate == 1, 0, Register::Size::Word,
                                          instr.vmad.cc);
                if (instr.generates_cc) {
                    LOG_CRITICAL(HW_GPU, "VMAD Generates an unhandled Control Code");
                    UNREACHABLE();
                }

                break;
            }
            case OpCode::Id::VSETP: {
                const std::string op_a = GetVideoOperandA(instr);
                const std::string op_b = GetVideoOperandB(instr);

                // We can't use the constant predicate as destination.
                ASSERT(instr.vsetp.pred3 != static_cast<u64>(Pred::UnusedIndex));

                const std::string second_pred = GetPredicateCondition(instr.vsetp.pred39, false);

                const std::string combiner = GetPredicateCombiner(instr.vsetp.op);

                const std::string predicate = GetPredicateComparison(instr.vsetp.cond, op_a, op_b);
                // Set the primary predicate to the result of Predicate OP SecondPredicate
                SetPredicate(instr.vsetp.pred3,
                             '(' + predicate + ") " + combiner + " (" + second_pred + ')');

                if (instr.vsetp.pred0 != static_cast<u64>(Pred::UnusedIndex)) {
                    // Set the secondary predicate to the result of !Predicate OP SecondPredicate,
                    // if enabled
                    SetPredicate(instr.vsetp.pred0,
                                 "!(" + predicate + ") " + combiner + " (" + second_pred + ')');
                }
                break;
            }
            default: {
                LOG_CRITICAL(HW_GPU, "Unhandled instruction: {}", opcode->get().GetName());
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

                // TODO(Subv): Figure out the actual depth of the flow stack, for now it seems
                // unlikely that shaders will use 20 nested SSYs and PBKs.
                constexpr u32 FLOW_STACK_SIZE = 20;
                shader.AddLine("uint flow_stack[" + std::to_string(FLOW_STACK_SIZE) + "];");
                shader.AddLine("uint flow_stack_top = 0u;");

                shader.AddLine("while (true) {");
                ++shader.scope;

                shader.AddLine("switch (jmp_to) {");

                for (auto label : labels) {
                    shader.AddLine("case " + std::to_string(label) + "u: {");
                    ++shader.scope;

                    const auto next_it = labels.lower_bound(label + 1);
                    const u32 next_label = next_it == labels.end() ? subroutine.end : *next_it;

                    const u32 compile_end = CompileRange(label, next_label);
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
    Tegra::Shader::Header header;
    const u32 main_offset;
    Maxwell3D::Regs::ShaderStage stage;
    const std::string& suffix;
    u64 local_memory_size;

    ShaderWriter shader;
    ShaderWriter declarations;
    GLSLRegisterManager regs{shader, declarations, stage, suffix, header};

    // Declarations
    std::set<std::string> declr_predicates;
}; // namespace OpenGL::GLShader::Decompiler

std::string GetCommonDeclarations() {
    return fmt::format("#define MAX_CONSTBUFFER_ELEMENTS {}\n",
                       RasterizerOpenGL::MaxConstbufferSize / sizeof(GLvec4));
}

std::optional<ProgramResult> DecompileProgram(const ProgramCode& program_code, u32 main_offset,
                                              Maxwell3D::Regs::ShaderStage stage,
                                              const std::string& suffix) {
    try {
        const auto subroutines =
            ControlFlowAnalyzer(program_code, main_offset, suffix).GetSubroutines();
        GLSLGenerator generator(subroutines, program_code, main_offset, stage, suffix);
        return ProgramResult{generator.GetShaderCode(), generator.GetEntries()};
    } catch (const DecompileFail& exception) {
        LOG_ERROR(HW_GPU, "Shader decompilation failed: {}", exception.what());
    }
    return {};
}

} // namespace OpenGL::GLShader::Decompiler
