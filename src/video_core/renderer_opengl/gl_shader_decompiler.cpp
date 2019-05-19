// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_device.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL::GLShader {

namespace {

using Tegra::Shader::Attribute;
using Tegra::Shader::AttributeUse;
using Tegra::Shader::Header;
using Tegra::Shader::IpaInterpMode;
using Tegra::Shader::IpaMode;
using Tegra::Shader::IpaSampleMode;
using Tegra::Shader::Register;
using namespace VideoCommon::Shader;

using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using ShaderStage = Tegra::Engines::Maxwell3D::Regs::ShaderStage;
using Operation = const OperationNode&;

enum class Type { Bool, Bool2, Float, Int, Uint, HalfFloat };

struct TextureAoffi {};
using TextureArgument = std::pair<Type, Node>;
using TextureIR = std::variant<TextureAoffi, TextureArgument>;

enum : u32 { POSITION_VARYING_LOCATION = 0, GENERIC_VARYING_START_LOCATION = 1 };
constexpr u32 MAX_CONSTBUFFER_ELEMENTS =
    static_cast<u32>(RasterizerOpenGL::MaxConstbufferSize) / (4 * sizeof(float));

class ShaderWriter {
public:
    void AddExpression(std::string_view text) {
        DEBUG_ASSERT(scope >= 0);
        if (!text.empty()) {
            AppendIndentation();
        }
        shader_source += text;
    }

    void AddLine(std::string_view text) {
        AddExpression(text);
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

    std::string GenerateTemporary() {
        std::string temporary = "tmp";
        temporary += std::to_string(temporary_index++);
        return temporary;
    }

    std::string GetResult() {
        return std::move(shader_source);
    }

    s32 scope = 0;

private:
    void AppendIndentation() {
        shader_source.append(static_cast<std::size_t>(scope) * 4, ' ');
    }

    std::string shader_source;
    u32 temporary_index = 1;
};

/// Generates code to use for a swizzle operation.
std::string GetSwizzle(u32 elem) {
    ASSERT(elem <= 3);
    std::string swizzle = ".";
    swizzle += "xyzw"[elem];
    return swizzle;
}

/// Translate topology
std::string GetTopologyName(Tegra::Shader::OutputTopology topology) {
    switch (topology) {
    case Tegra::Shader::OutputTopology::PointList:
        return "points";
    case Tegra::Shader::OutputTopology::LineStrip:
        return "line_strip";
    case Tegra::Shader::OutputTopology::TriangleStrip:
        return "triangle_strip";
    default:
        UNIMPLEMENTED_MSG("Unknown output topology: {}", static_cast<u32>(topology));
        return "points";
    }
}

/// Returns true if an object has to be treated as precise
bool IsPrecise(Operation operand) {
    const auto& meta{operand.GetMeta()};
    if (const auto arithmetic = std::get_if<MetaArithmetic>(&meta)) {
        return arithmetic->precise;
    }
    return false;
}

bool IsPrecise(Node node) {
    if (const auto operation = std::get_if<OperationNode>(node)) {
        return IsPrecise(*operation);
    }
    return false;
}

constexpr bool IsGenericAttribute(Attribute::Index index) {
    return index >= Attribute::Index::Attribute_0 && index <= Attribute::Index::Attribute_31;
}

constexpr Attribute::Index ToGenericAttribute(u32 value) {
    return static_cast<Attribute::Index>(value + static_cast<u32>(Attribute::Index::Attribute_0));
}

u32 GetGenericAttributeIndex(Attribute::Index index) {
    ASSERT(IsGenericAttribute(index));
    return static_cast<u32>(index) - static_cast<u32>(Attribute::Index::Attribute_0);
}

class GLSLDecompiler final {
public:
    explicit GLSLDecompiler(const Device& device, const ShaderIR& ir, ShaderStage stage,
                            std::string suffix)
        : device{device}, ir{ir}, stage{stage}, suffix{suffix}, header{ir.GetHeader()} {}

    void Decompile() {
        DeclareVertex();
        DeclareGeometry();
        DeclareRegisters();
        DeclarePredicates();
        DeclareLocalMemory();
        DeclareInternalFlags();
        DeclareInputAttributes();
        DeclareOutputAttributes();
        DeclareConstantBuffers();
        DeclareGlobalMemory();
        DeclareSamplers();
        DeclarePhysicalAttributeReader();

        code.AddLine("void execute_" + suffix + "() {");
        ++code.scope;

        // VM's program counter
        const auto first_address = ir.GetBasicBlocks().begin()->first;
        code.AddLine("uint jmp_to = " + std::to_string(first_address) + "u;");

        // TODO(Subv): Figure out the actual depth of the flow stack, for now it seems
        // unlikely that shaders will use 20 nested SSYs and PBKs.
        constexpr u32 FLOW_STACK_SIZE = 20;
        code.AddLine(fmt::format("uint flow_stack[{}];", FLOW_STACK_SIZE));
        code.AddLine("uint flow_stack_top = 0u;");

        code.AddLine("while (true) {");
        ++code.scope;

        code.AddLine("switch (jmp_to) {");

        for (const auto& pair : ir.GetBasicBlocks()) {
            const auto [address, bb] = pair;
            code.AddLine(fmt::format("case 0x{:x}u: {{", address));
            ++code.scope;

            VisitBlock(bb);

            --code.scope;
            code.AddLine('}');
        }

        code.AddLine("default: return;");
        code.AddLine('}');

        for (std::size_t i = 0; i < 2; ++i) {
            --code.scope;
            code.AddLine('}');
        }
    }

    std::string GetResult() {
        return code.GetResult();
    }

    ShaderEntries GetShaderEntries() const {
        ShaderEntries entries;
        for (const auto& cbuf : ir.GetConstantBuffers()) {
            entries.const_buffers.emplace_back(cbuf.second.GetMaxOffset(), cbuf.second.IsIndirect(),
                                               cbuf.first);
        }
        for (const auto& sampler : ir.GetSamplers()) {
            entries.samplers.emplace_back(sampler);
        }
        for (const auto& gmem_pair : ir.GetGlobalMemory()) {
            const auto& [base, usage] = gmem_pair;
            entries.global_memory_entries.emplace_back(base.cbuf_index, base.cbuf_offset,
                                                       usage.is_read, usage.is_written);
        }
        entries.clip_distances = ir.GetClipDistances();
        entries.shader_length = ir.GetLength();
        return entries;
    }

private:
    using OperationDecompilerFn = std::string (GLSLDecompiler::*)(Operation);
    using OperationDecompilersArray =
        std::array<OperationDecompilerFn, static_cast<std::size_t>(OperationCode::Amount)>;

    void DeclareVertex() {
        if (stage != ShaderStage::Vertex)
            return;

        DeclareVertexRedeclarations();
    }

    void DeclareGeometry() {
        if (stage != ShaderStage::Geometry)
            return;

        const auto topology = GetTopologyName(header.common3.output_topology);
        const auto max_vertices = std::to_string(header.common4.max_output_vertices);
        code.AddLine("layout (" + topology + ", max_vertices = " + max_vertices + ") out;");
        code.AddNewLine();

        DeclareVertexRedeclarations();
    }

    void DeclareVertexRedeclarations() {
        bool clip_distances_declared = false;

        code.AddLine("out gl_PerVertex {");
        ++code.scope;

        code.AddLine("vec4 gl_Position;");

        for (const auto o : ir.GetOutputAttributes()) {
            if (o == Attribute::Index::PointSize)
                code.AddLine("float gl_PointSize;");
            if (!clip_distances_declared && (o == Attribute::Index::ClipDistances0123 ||
                                             o == Attribute::Index::ClipDistances4567)) {
                code.AddLine("float gl_ClipDistance[];");
                clip_distances_declared = true;
            }
        }

        --code.scope;
        code.AddLine("};");
        code.AddNewLine();
    }

    void DeclareRegisters() {
        const auto& registers = ir.GetRegisters();
        for (const u32 gpr : registers) {
            code.AddLine("float " + GetRegister(gpr) + " = 0;");
        }
        if (!registers.empty())
            code.AddNewLine();
    }

    void DeclarePredicates() {
        const auto& predicates = ir.GetPredicates();
        for (const auto pred : predicates) {
            code.AddLine("bool " + GetPredicate(pred) + " = false;");
        }
        if (!predicates.empty())
            code.AddNewLine();
    }

    void DeclareLocalMemory() {
        if (const u64 local_memory_size = header.GetLocalMemorySize(); local_memory_size > 0) {
            const auto element_count = Common::AlignUp(local_memory_size, 4) / 4;
            code.AddLine("float " + GetLocalMemory() + '[' + std::to_string(element_count) + "];");
            code.AddNewLine();
        }
    }

    void DeclareInternalFlags() {
        for (u32 flag = 0; flag < static_cast<u32>(InternalFlag::Amount); flag++) {
            const InternalFlag flag_code = static_cast<InternalFlag>(flag);
            code.AddLine("bool " + GetInternalFlag(flag_code) + " = false;");
        }
        code.AddNewLine();
    }

    std::string GetInputFlags(AttributeUse attribute) {
        switch (attribute) {
        case AttributeUse::Perspective:
            // Default, Smooth
            return {};
        case AttributeUse::Constant:
            return "flat ";
        case AttributeUse::ScreenLinear:
            return "noperspective ";
        default:
        case AttributeUse::Unused:
            UNREACHABLE_MSG("Unused attribute being fetched");
            return {};
            UNIMPLEMENTED_MSG("Unknown attribute usage index={}", static_cast<u32>(attribute));
            return {};
        }
    }

    void DeclareInputAttributes() {
        if (ir.HasPhysicalAttributes()) {
            const u32 num_inputs{GetNumPhysicalInputAttributes()};
            for (u32 i = 0; i < num_inputs; ++i) {
                DeclareInputAttribute(ToGenericAttribute(i), true);
            }
            code.AddNewLine();
            return;
        }

        const auto& attributes = ir.GetInputAttributes();
        for (const auto index : attributes) {
            if (IsGenericAttribute(index)) {
                DeclareInputAttribute(index, false);
            }
        }
        if (!attributes.empty())
            code.AddNewLine();
    }

    void DeclareInputAttribute(Attribute::Index index, bool skip_unused) {
        const u32 generic_index{GetGenericAttributeIndex(index)};

        std::string name{GetInputAttribute(index)};
        if (stage == ShaderStage::Geometry) {
            name = "gs_" + name + "[]";
        }

        std::string suffix;
        if (stage == ShaderStage::Fragment) {
            const auto input_mode{header.ps.GetAttributeUse(generic_index)};
            if (skip_unused && input_mode == AttributeUse::Unused) {
                return;
            }
            suffix = GetInputFlags(input_mode);
        }

        u32 location = generic_index;
        if (stage != ShaderStage::Vertex) {
            // If inputs are varyings, add an offset
            location += GENERIC_VARYING_START_LOCATION;
        }

        code.AddLine("layout (location = " + std::to_string(location) + ") " + suffix + "in vec4 " +
                     name + ';');
    }

    void DeclareOutputAttributes() {
        if (ir.HasPhysicalAttributes() && stage != ShaderStage::Fragment) {
            for (u32 i = 0; i < GetNumPhysicalVaryings(); ++i) {
                DeclareOutputAttribute(ToGenericAttribute(i));
            }
            code.AddNewLine();
            return;
        }

        const auto& attributes = ir.GetOutputAttributes();
        for (const auto index : attributes) {
            if (IsGenericAttribute(index)) {
                DeclareOutputAttribute(index);
            }
        }
        if (!attributes.empty())
            code.AddNewLine();
    }

    void DeclareOutputAttribute(Attribute::Index index) {
        const u32 location{GetGenericAttributeIndex(index) + GENERIC_VARYING_START_LOCATION};
        code.AddLine("layout (location = " + std::to_string(location) + ") out vec4 " +
                     GetOutputAttribute(index) + ';');
    }

    void DeclareConstantBuffers() {
        for (const auto& entry : ir.GetConstantBuffers()) {
            const auto [index, size] = entry;
            code.AddLine("layout (std140, binding = CBUF_BINDING_" + std::to_string(index) +
                         ") uniform " + GetConstBufferBlock(index) + " {");
            code.AddLine("    vec4 " + GetConstBuffer(index) + "[MAX_CONSTBUFFER_ELEMENTS];");
            code.AddLine("};");
            code.AddNewLine();
        }
    }

    void DeclareGlobalMemory() {
        for (const auto& gmem : ir.GetGlobalMemory()) {
            const auto& [base, usage] = gmem;

            // Since we don't know how the shader will use the shader, hint the driver to disable as
            // much optimizations as possible
            std::string qualifier = "coherent volatile";
            if (usage.is_read && !usage.is_written)
                qualifier += " readonly";
            else if (usage.is_written && !usage.is_read)
                qualifier += " writeonly";

            const std::string binding =
                fmt::format("GMEM_BINDING_{}_{}", base.cbuf_index, base.cbuf_offset);
            code.AddLine("layout (std430, binding = " + binding + ") " + qualifier + " buffer " +
                         GetGlobalMemoryBlock(base) + " {");
            code.AddLine("    float " + GetGlobalMemory(base) + "[];");
            code.AddLine("};");
            code.AddNewLine();
        }
    }

    void DeclareSamplers() {
        const auto& samplers = ir.GetSamplers();
        for (const auto& sampler : samplers) {
            std::string sampler_type = [&]() {
                switch (sampler.GetType()) {
                case Tegra::Shader::TextureType::Texture1D:
                    return "sampler1D";
                case Tegra::Shader::TextureType::Texture2D:
                    return "sampler2D";
                case Tegra::Shader::TextureType::Texture3D:
                    return "sampler3D";
                case Tegra::Shader::TextureType::TextureCube:
                    return "samplerCube";
                default:
                    UNREACHABLE();
                    return "sampler2D";
                }
            }();
            if (sampler.IsArray())
                sampler_type += "Array";
            if (sampler.IsShadow())
                sampler_type += "Shadow";

            code.AddLine("layout (binding = SAMPLER_BINDING_" + std::to_string(sampler.GetIndex()) +
                         ") uniform " + sampler_type + ' ' + GetSampler(sampler) + ';');
        }
        if (!samplers.empty())
            code.AddNewLine();
    }

    void DeclarePhysicalAttributeReader() {
        if (!ir.HasPhysicalAttributes()) {
            return;
        }
        code.AddLine("float readPhysicalAttribute(uint physical_address) {");
        ++code.scope;
        code.AddLine("switch (physical_address) {");

        // Just declare generic attributes for now.
        const auto num_attributes{static_cast<u32>(GetNumPhysicalInputAttributes())};
        for (u32 index = 0; index < num_attributes; ++index) {
            const auto attribute{ToGenericAttribute(index)};
            for (u32 element = 0; element < 4; ++element) {
                constexpr u32 generic_base{0x80};
                constexpr u32 generic_stride{16};
                constexpr u32 element_stride{4};
                const u32 address{generic_base + index * generic_stride + element * element_stride};

                const bool declared{stage != ShaderStage::Fragment ||
                                    header.ps.GetAttributeUse(index) != AttributeUse::Unused};
                const std::string value{declared ? ReadAttribute(attribute, element) : "0"};
                code.AddLine(fmt::format("case 0x{:x}: return {};", address, value));
            }
        }

        code.AddLine("default: return 0;");

        code.AddLine('}');
        --code.scope;
        code.AddLine('}');
        code.AddNewLine();
    }

    void VisitBlock(const NodeBlock& bb) {
        for (const Node node : bb) {
            if (const std::string expr = Visit(node); !expr.empty()) {
                code.AddLine(expr);
            }
        }
    }

    std::string Visit(Node node) {
        if (const auto operation = std::get_if<OperationNode>(node)) {
            const auto operation_index = static_cast<std::size_t>(operation->GetCode());
            if (operation_index >= operation_decompilers.size()) {
                UNREACHABLE_MSG("Out of bounds operation: {}", operation_index);
                return {};
            }
            const auto decompiler = operation_decompilers[operation_index];
            if (decompiler == nullptr) {
                UNREACHABLE_MSG("Undefined operation: {}", operation_index);
                return {};
            }
            return (this->*decompiler)(*operation);

        } else if (const auto gpr = std::get_if<GprNode>(node)) {
            const u32 index = gpr->GetIndex();
            if (index == Register::ZeroIndex) {
                return "0";
            }
            return GetRegister(index);

        } else if (const auto immediate = std::get_if<ImmediateNode>(node)) {
            const u32 value = immediate->GetValue();
            if (value < 10) {
                // For eyecandy avoid using hex numbers on single digits
                return fmt::format("utof({}u)", immediate->GetValue());
            }
            return fmt::format("utof(0x{:x}u)", immediate->GetValue());

        } else if (const auto predicate = std::get_if<PredicateNode>(node)) {
            const auto value = [&]() -> std::string {
                switch (const auto index = predicate->GetIndex(); index) {
                case Tegra::Shader::Pred::UnusedIndex:
                    return "true";
                case Tegra::Shader::Pred::NeverExecute:
                    return "false";
                default:
                    return GetPredicate(index);
                }
            }();
            if (predicate->IsNegated()) {
                return "!(" + value + ')';
            }
            return value;

        } else if (const auto abuf = std::get_if<AbufNode>(node)) {
            UNIMPLEMENTED_IF_MSG(abuf->IsPhysicalBuffer() && stage == ShaderStage::Geometry,
                                 "Physical attributes in geometry shaders are not implemented");
            if (abuf->IsPhysicalBuffer()) {
                return "readPhysicalAttribute(ftou(" + Visit(abuf->GetPhysicalAddress()) + "))";
            }
            return ReadAttribute(abuf->GetIndex(), abuf->GetElement(), abuf->GetBuffer());

        } else if (const auto cbuf = std::get_if<CbufNode>(node)) {
            const Node offset = cbuf->GetOffset();
            if (const auto immediate = std::get_if<ImmediateNode>(offset)) {
                // Direct access
                const u32 offset_imm = immediate->GetValue();
                ASSERT_MSG(offset_imm % 4 == 0, "Unaligned cbuf direct access");
                return fmt::format("{}[{}][{}]", GetConstBuffer(cbuf->GetIndex()),
                                   offset_imm / (4 * 4), (offset_imm / 4) % 4);

            } else if (std::holds_alternative<OperationNode>(*offset)) {
                // Indirect access
                const std::string final_offset = code.GenerateTemporary();
                code.AddLine("uint " + final_offset + " = (ftou(" + Visit(offset) + ") / 4);");
                return fmt::format("{}[{} / 4][{} % 4]", GetConstBuffer(cbuf->GetIndex()),
                                   final_offset, final_offset);

            } else {
                UNREACHABLE_MSG("Unmanaged offset node type");
            }

        } else if (const auto gmem = std::get_if<GmemNode>(node)) {
            const std::string real = Visit(gmem->GetRealAddress());
            const std::string base = Visit(gmem->GetBaseAddress());
            const std::string final_offset = "(ftou(" + real + ") - ftou(" + base + ")) / 4";
            return fmt::format("{}[{}]", GetGlobalMemory(gmem->GetDescriptor()), final_offset);

        } else if (const auto lmem = std::get_if<LmemNode>(node)) {
            return fmt::format("{}[ftou({}) / 4]", GetLocalMemory(), Visit(lmem->GetAddress()));

        } else if (const auto internal_flag = std::get_if<InternalFlagNode>(node)) {
            return GetInternalFlag(internal_flag->GetFlag());

        } else if (const auto conditional = std::get_if<ConditionalNode>(node)) {
            // It's invalid to call conditional on nested nodes, use an operation instead
            code.AddLine("if (" + Visit(conditional->GetCondition()) + ") {");
            ++code.scope;

            VisitBlock(conditional->GetCode());

            --code.scope;
            code.AddLine('}');
            return {};

        } else if (const auto comment = std::get_if<CommentNode>(node)) {
            return "// " + comment->GetText();
        }
        UNREACHABLE();
        return {};
    }

    std::string ReadAttribute(Attribute::Index attribute, u32 element, Node buffer = {}) {
        const auto GeometryPass = [&](std::string name) {
            if (stage == ShaderStage::Geometry && buffer) {
                // TODO(Rodrigo): Guard geometry inputs against out of bound reads. Some games
                // set an 0x80000000 index for those and the shader fails to build. Find out why
                // this happens and what's its intent.
                return "gs_" + std::move(name) + "[ftou(" + Visit(buffer) + ") % MAX_VERTEX_INPUT]";
            }
            return name;
        };

        switch (attribute) {
        case Attribute::Index::Position:
            if (stage != ShaderStage::Fragment) {
                return GeometryPass("position") + GetSwizzle(element);
            } else {
                return element == 3 ? "1.0f" : "gl_FragCoord" + GetSwizzle(element);
            }
        case Attribute::Index::PointCoord:
            switch (element) {
            case 0:
                return "gl_PointCoord.x";
            case 1:
                return "gl_PointCoord.y";
            case 2:
            case 3:
                return "0";
            }
            UNREACHABLE();
            return "0";
        case Attribute::Index::TessCoordInstanceIDVertexID:
            // TODO(Subv): Find out what the values are for the first two elements when inside a
            // vertex shader, and what's the value of the fourth element when inside a Tess Eval
            // shader.
            ASSERT(stage == ShaderStage::Vertex);
            switch (element) {
            case 2:
                // Config pack's first value is instance_id.
                return "uintBitsToFloat(config_pack[0])";
            case 3:
                return "uintBitsToFloat(gl_VertexID)";
            }
            UNIMPLEMENTED_MSG("Unmanaged TessCoordInstanceIDVertexID element={}", element);
            return "0";
        case Attribute::Index::FrontFacing:
            // TODO(Subv): Find out what the values are for the other elements.
            ASSERT(stage == ShaderStage::Fragment);
            switch (element) {
            case 3:
                return "itof(gl_FrontFacing ? -1 : 0)";
            }
            UNIMPLEMENTED_MSG("Unmanaged FrontFacing element={}", element);
            return "0";
        default:
            if (IsGenericAttribute(attribute)) {
                return GeometryPass(GetInputAttribute(attribute)) + GetSwizzle(element);
            }
            break;
        }
        UNIMPLEMENTED_MSG("Unhandled input attribute: {}", static_cast<u32>(attribute));
        return "0";
    }

    std::string ApplyPrecise(Operation operation, const std::string& value) {
        if (!IsPrecise(operation)) {
            return value;
        }
        // There's a bug in NVidia's proprietary drivers that makes precise fail on fragment shaders
        const std::string precise = stage != ShaderStage::Fragment ? "precise " : "";

        const std::string temporary = code.GenerateTemporary();
        code.AddLine(precise + "float " + temporary + " = " + value + ';');
        return temporary;
    }

    std::string VisitOperand(Operation operation, std::size_t operand_index) {
        const auto& operand = operation[operand_index];
        const bool parent_precise = IsPrecise(operation);
        const bool child_precise = IsPrecise(operand);
        const bool child_trivial = !std::holds_alternative<OperationNode>(*operand);
        if (!parent_precise || child_precise || child_trivial) {
            return Visit(operand);
        }

        const std::string temporary = code.GenerateTemporary();
        code.AddLine("float " + temporary + " = " + Visit(operand) + ';');
        return temporary;
    }

    std::string VisitOperand(Operation operation, std::size_t operand_index, Type type) {
        return CastOperand(VisitOperand(operation, operand_index), type);
    }

    std::string CastOperand(const std::string& value, Type type) const {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            return value;
        case Type::Int:
            return "ftoi(" + value + ')';
        case Type::Uint:
            return "ftou(" + value + ')';
        case Type::HalfFloat:
            return "toHalf2(" + value + ')';
        }
        UNREACHABLE();
        return value;
    }

    std::string BitwiseCastResult(std::string value, Type type, bool needs_parenthesis = false) {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            if (needs_parenthesis) {
                return '(' + value + ')';
            }
            return value;
        case Type::Int:
            return "itof(" + value + ')';
        case Type::Uint:
            return "utof(" + value + ')';
        case Type::HalfFloat:
            return "fromHalf2(" + value + ')';
        }
        UNREACHABLE();
        return value;
    }

    std::string GenerateUnary(Operation operation, const std::string& func, Type result_type,
                              Type type_a, bool needs_parenthesis = true) {
        return ApplyPrecise(operation,
                            BitwiseCastResult(func + '(' + VisitOperand(operation, 0, type_a) + ')',
                                              result_type, needs_parenthesis));
    }

    std::string GenerateBinaryInfix(Operation operation, const std::string& func, Type result_type,
                                    Type type_a, Type type_b) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);

        return ApplyPrecise(
            operation, BitwiseCastResult('(' + op_a + ' ' + func + ' ' + op_b + ')', result_type));
    }

    std::string GenerateBinaryCall(Operation operation, const std::string& func, Type result_type,
                                   Type type_a, Type type_b) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);

        return ApplyPrecise(operation,
                            BitwiseCastResult(func + '(' + op_a + ", " + op_b + ')', result_type));
    }

    std::string GenerateTernary(Operation operation, const std::string& func, Type result_type,
                                Type type_a, Type type_b, Type type_c) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_c = VisitOperand(operation, 2, type_c);

        return ApplyPrecise(
            operation,
            BitwiseCastResult(func + '(' + op_a + ", " + op_b + ", " + op_c + ')', result_type));
    }

    std::string GenerateQuaternary(Operation operation, const std::string& func, Type result_type,
                                   Type type_a, Type type_b, Type type_c, Type type_d) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_c = VisitOperand(operation, 2, type_c);
        const std::string op_d = VisitOperand(operation, 3, type_d);

        return ApplyPrecise(operation, BitwiseCastResult(func + '(' + op_a + ", " + op_b + ", " +
                                                             op_c + ", " + op_d + ')',
                                                         result_type));
    }

    std::string GenerateTexture(Operation operation, const std::string& function_suffix,
                                const std::vector<TextureIR>& extras) {
        constexpr std::array<const char*, 4> coord_constructors = {"float", "vec2", "vec3", "vec4"};

        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        const std::size_t count = operation.GetOperandsCount();
        const bool has_array = meta->sampler.IsArray();
        const bool has_shadow = meta->sampler.IsShadow();

        std::string expr = "texture" + function_suffix;
        if (!meta->aoffi.empty()) {
            expr += "Offset";
        }
        expr += '(' + GetSampler(meta->sampler) + ", ";
        expr += coord_constructors.at(count + (has_array ? 1 : 0) + (has_shadow ? 1 : 0) - 1);
        expr += '(';
        for (std::size_t i = 0; i < count; ++i) {
            expr += Visit(operation[i]);

            const std::size_t next = i + 1;
            if (next < count)
                expr += ", ";
        }
        if (has_array) {
            expr += ", float(ftoi(" + Visit(meta->array) + "))";
        }
        if (has_shadow) {
            expr += ", " + Visit(meta->depth_compare);
        }
        expr += ')';

        for (const auto& variant : extras) {
            if (const auto argument = std::get_if<TextureArgument>(&variant)) {
                expr += GenerateTextureArgument(*argument);
            } else if (std::get_if<TextureAoffi>(&variant)) {
                expr += GenerateTextureAoffi(meta->aoffi);
            } else {
                UNREACHABLE();
            }
        }

        return expr + ')';
    }

    std::string GenerateTextureArgument(TextureArgument argument) {
        const auto [type, operand] = argument;
        if (operand == nullptr) {
            return {};
        }

        std::string expr = ", ";
        switch (type) {
        case Type::Int:
            if (const auto immediate = std::get_if<ImmediateNode>(operand)) {
                // Inline the string as an immediate integer in GLSL (some extra arguments are
                // required to be constant)
                expr += std::to_string(static_cast<s32>(immediate->GetValue()));
            } else {
                expr += "ftoi(" + Visit(operand) + ')';
            }
            break;
        case Type::Float:
            expr += Visit(operand);
            break;
        default: {
            const auto type_int = static_cast<u32>(type);
            UNIMPLEMENTED_MSG("Unimplemented extra type={}", type_int);
            expr += '0';
            break;
        }
        }
        return expr;
    }

    std::string GenerateTextureAoffi(const std::vector<Node>& aoffi) {
        if (aoffi.empty()) {
            return {};
        }
        constexpr std::array<const char*, 3> coord_constructors = {"int", "ivec2", "ivec3"};
        std::string expr = ", ";
        expr += coord_constructors.at(aoffi.size() - 1);
        expr += '(';

        for (std::size_t index = 0; index < aoffi.size(); ++index) {
            const auto operand{aoffi.at(index)};
            if (const auto immediate = std::get_if<ImmediateNode>(operand)) {
                // Inline the string as an immediate integer in GLSL (AOFFI arguments are required
                // to be constant by the standard).
                expr += std::to_string(static_cast<s32>(immediate->GetValue()));
            } else if (device.HasVariableAoffi()) {
                // Avoid using variable AOFFI on unsupported devices.
                expr += "ftoi(" + Visit(operand) + ')';
            } else {
                // Insert 0 on devices not supporting variable AOFFI.
                expr += '0';
            }
            if (index + 1 < aoffi.size()) {
                expr += ", ";
            }
        }
        expr += ')';

        return expr;
    }

    std::string Assign(Operation operation) {
        const Node dest = operation[0];
        const Node src = operation[1];

        std::string target;
        if (const auto gpr = std::get_if<GprNode>(dest)) {
            if (gpr->GetIndex() == Register::ZeroIndex) {
                // Writing to Register::ZeroIndex is a no op
                return {};
            }
            target = GetRegister(gpr->GetIndex());

        } else if (const auto abuf = std::get_if<AbufNode>(dest)) {
            UNIMPLEMENTED_IF(abuf->IsPhysicalBuffer());

            target = [&]() -> std::string {
                switch (const auto attribute = abuf->GetIndex(); abuf->GetIndex()) {
                case Attribute::Index::Position:
                    return "position" + GetSwizzle(abuf->GetElement());
                case Attribute::Index::PointSize:
                    return "gl_PointSize";
                case Attribute::Index::ClipDistances0123:
                    return "gl_ClipDistance[" + std::to_string(abuf->GetElement()) + ']';
                case Attribute::Index::ClipDistances4567:
                    return "gl_ClipDistance[" + std::to_string(abuf->GetElement() + 4) + ']';
                default:
                    if (IsGenericAttribute(attribute)) {
                        return GetOutputAttribute(attribute) + GetSwizzle(abuf->GetElement());
                    }
                    UNIMPLEMENTED_MSG("Unhandled output attribute: {}",
                                      static_cast<u32>(attribute));
                    return "0";
                }
            }();

        } else if (const auto lmem = std::get_if<LmemNode>(dest)) {
            target = GetLocalMemory() + "[ftou(" + Visit(lmem->GetAddress()) + ") / 4]";

        } else if (const auto gmem = std::get_if<GmemNode>(dest)) {
            const std::string real = Visit(gmem->GetRealAddress());
            const std::string base = Visit(gmem->GetBaseAddress());
            const std::string final_offset = "(ftou(" + real + ") - ftou(" + base + ")) / 4";
            target = fmt::format("{}[{}]", GetGlobalMemory(gmem->GetDescriptor()), final_offset);

        } else {
            UNREACHABLE_MSG("Assign called without a proper target");
        }

        code.AddLine(target + " = " + Visit(src) + ';');
        return {};
    }

    template <Type type>
    std::string Add(Operation operation) {
        return GenerateBinaryInfix(operation, "+", type, type, type);
    }

    template <Type type>
    std::string Mul(Operation operation) {
        return GenerateBinaryInfix(operation, "*", type, type, type);
    }

    template <Type type>
    std::string Div(Operation operation) {
        return GenerateBinaryInfix(operation, "/", type, type, type);
    }

    template <Type type>
    std::string Fma(Operation operation) {
        return GenerateTernary(operation, "fma", type, type, type, type);
    }

    template <Type type>
    std::string Negate(Operation operation) {
        return GenerateUnary(operation, "-", type, type, true);
    }

    template <Type type>
    std::string Absolute(Operation operation) {
        return GenerateUnary(operation, "abs", type, type, false);
    }

    std::string FClamp(Operation operation) {
        return GenerateTernary(operation, "clamp", Type::Float, Type::Float, Type::Float,
                               Type::Float);
    }

    template <Type type>
    std::string Min(Operation operation) {
        return GenerateBinaryCall(operation, "min", type, type, type);
    }

    template <Type type>
    std::string Max(Operation operation) {
        return GenerateBinaryCall(operation, "max", type, type, type);
    }

    std::string Select(Operation operation) {
        const std::string condition = Visit(operation[0]);
        const std::string true_case = Visit(operation[1]);
        const std::string false_case = Visit(operation[2]);
        return ApplyPrecise(operation,
                            '(' + condition + " ? " + true_case + " : " + false_case + ')');
    }

    std::string FCos(Operation operation) {
        return GenerateUnary(operation, "cos", Type::Float, Type::Float, false);
    }

    std::string FSin(Operation operation) {
        return GenerateUnary(operation, "sin", Type::Float, Type::Float, false);
    }

    std::string FExp2(Operation operation) {
        return GenerateUnary(operation, "exp2", Type::Float, Type::Float, false);
    }

    std::string FLog2(Operation operation) {
        return GenerateUnary(operation, "log2", Type::Float, Type::Float, false);
    }

    std::string FInverseSqrt(Operation operation) {
        return GenerateUnary(operation, "inversesqrt", Type::Float, Type::Float, false);
    }

    std::string FSqrt(Operation operation) {
        return GenerateUnary(operation, "sqrt", Type::Float, Type::Float, false);
    }

    std::string FRoundEven(Operation operation) {
        return GenerateUnary(operation, "roundEven", Type::Float, Type::Float, false);
    }

    std::string FFloor(Operation operation) {
        return GenerateUnary(operation, "floor", Type::Float, Type::Float, false);
    }

    std::string FCeil(Operation operation) {
        return GenerateUnary(operation, "ceil", Type::Float, Type::Float, false);
    }

    std::string FTrunc(Operation operation) {
        return GenerateUnary(operation, "trunc", Type::Float, Type::Float, false);
    }

    template <Type type>
    std::string FCastInteger(Operation operation) {
        return GenerateUnary(operation, "float", Type::Float, type, false);
    }

    std::string ICastFloat(Operation operation) {
        return GenerateUnary(operation, "int", Type::Int, Type::Float, false);
    }

    std::string ICastUnsigned(Operation operation) {
        return GenerateUnary(operation, "int", Type::Int, Type::Uint, false);
    }

    template <Type type>
    std::string LogicalShiftLeft(Operation operation) {
        return GenerateBinaryInfix(operation, "<<", type, type, Type::Uint);
    }

    std::string ILogicalShiftRight(Operation operation) {
        const std::string op_a = VisitOperand(operation, 0, Type::Uint);
        const std::string op_b = VisitOperand(operation, 1, Type::Uint);

        return ApplyPrecise(operation,
                            BitwiseCastResult("int(" + op_a + " >> " + op_b + ')', Type::Int));
    }

    std::string IArithmeticShiftRight(Operation operation) {
        return GenerateBinaryInfix(operation, ">>", Type::Int, Type::Int, Type::Uint);
    }

    template <Type type>
    std::string BitwiseAnd(Operation operation) {
        return GenerateBinaryInfix(operation, "&", type, type, type);
    }

    template <Type type>
    std::string BitwiseOr(Operation operation) {
        return GenerateBinaryInfix(operation, "|", type, type, type);
    }

    template <Type type>
    std::string BitwiseXor(Operation operation) {
        return GenerateBinaryInfix(operation, "^", type, type, type);
    }

    template <Type type>
    std::string BitwiseNot(Operation operation) {
        return GenerateUnary(operation, "~", type, type, false);
    }

    std::string UCastFloat(Operation operation) {
        return GenerateUnary(operation, "uint", Type::Uint, Type::Float, false);
    }

    std::string UCastSigned(Operation operation) {
        return GenerateUnary(operation, "uint", Type::Uint, Type::Int, false);
    }

    std::string UShiftRight(Operation operation) {
        return GenerateBinaryInfix(operation, ">>", Type::Uint, Type::Uint, Type::Uint);
    }

    template <Type type>
    std::string BitfieldInsert(Operation operation) {
        return GenerateQuaternary(operation, "bitfieldInsert", type, type, type, Type::Int,
                                  Type::Int);
    }

    template <Type type>
    std::string BitfieldExtract(Operation operation) {
        return GenerateTernary(operation, "bitfieldExtract", type, type, Type::Int, Type::Int);
    }

    template <Type type>
    std::string BitCount(Operation operation) {
        return GenerateUnary(operation, "bitCount", type, type, false);
    }

    std::string HNegate(Operation operation) {
        const auto GetNegate = [&](std::size_t index) -> std::string {
            return VisitOperand(operation, index, Type::Bool) + " ? -1 : 1";
        };
        const std::string value = '(' + VisitOperand(operation, 0, Type::HalfFloat) + " * vec2(" +
                                  GetNegate(1) + ", " + GetNegate(2) + "))";
        return BitwiseCastResult(value, Type::HalfFloat);
    }

    std::string HClamp(Operation operation) {
        const std::string value = VisitOperand(operation, 0, Type::HalfFloat);
        const std::string min = VisitOperand(operation, 1, Type::Float);
        const std::string max = VisitOperand(operation, 2, Type::Float);
        const std::string clamped = "clamp(" + value + ", vec2(" + min + "), vec2(" + max + "))";
        return ApplyPrecise(operation, BitwiseCastResult(clamped, Type::HalfFloat));
    }

    std::string HUnpack(Operation operation) {
        const std::string operand{VisitOperand(operation, 0, Type::HalfFloat)};
        const auto value = [&]() -> std::string {
            switch (std::get<Tegra::Shader::HalfType>(operation.GetMeta())) {
            case Tegra::Shader::HalfType::H0_H1:
                return operand;
            case Tegra::Shader::HalfType::F32:
                return "vec2(fromHalf2(" + operand + "))";
            case Tegra::Shader::HalfType::H0_H0:
                return "vec2(" + operand + "[0])";
            case Tegra::Shader::HalfType::H1_H1:
                return "vec2(" + operand + "[1])";
            }
            UNREACHABLE();
            return "0";
        }();
        return "fromHalf2(" + value + ')';
    }

    std::string HMergeF32(Operation operation) {
        return "float(toHalf2(" + Visit(operation[0]) + ")[0])";
    }

    std::string HMergeH0(Operation operation) {
        return "fromHalf2(vec2(toHalf2(" + Visit(operation[1]) + ")[0], toHalf2(" +
               Visit(operation[0]) + ")[1]))";
    }

    std::string HMergeH1(Operation operation) {
        return "fromHalf2(vec2(toHalf2(" + Visit(operation[0]) + ")[0], toHalf2(" +
               Visit(operation[1]) + ")[1]))";
    }

    std::string HPack2(Operation operation) {
        return "utof(packHalf2x16(vec2(" + Visit(operation[0]) + ", " + Visit(operation[1]) + ")))";
    }

    template <Type type>
    std::string LogicalLessThan(Operation operation) {
        return GenerateBinaryInfix(operation, "<", Type::Bool, type, type);
    }

    template <Type type>
    std::string LogicalEqual(Operation operation) {
        return GenerateBinaryInfix(operation, "==", Type::Bool, type, type);
    }

    template <Type type>
    std::string LogicalLessEqual(Operation operation) {
        return GenerateBinaryInfix(operation, "<=", Type::Bool, type, type);
    }

    template <Type type>
    std::string LogicalGreaterThan(Operation operation) {
        return GenerateBinaryInfix(operation, ">", Type::Bool, type, type);
    }

    template <Type type>
    std::string LogicalNotEqual(Operation operation) {
        return GenerateBinaryInfix(operation, "!=", Type::Bool, type, type);
    }

    template <Type type>
    std::string LogicalGreaterEqual(Operation operation) {
        return GenerateBinaryInfix(operation, ">=", Type::Bool, type, type);
    }

    std::string LogicalFIsNan(Operation operation) {
        return GenerateUnary(operation, "isnan", Type::Bool, Type::Float, false);
    }

    std::string LogicalAssign(Operation operation) {
        const Node dest = operation[0];
        const Node src = operation[1];

        std::string target;

        if (const auto pred = std::get_if<PredicateNode>(dest)) {
            ASSERT_MSG(!pred->IsNegated(), "Negating logical assignment");

            const auto index = pred->GetIndex();
            switch (index) {
            case Tegra::Shader::Pred::NeverExecute:
            case Tegra::Shader::Pred::UnusedIndex:
                // Writing to these predicates is a no-op
                return {};
            }
            target = GetPredicate(index);
        } else if (const auto flag = std::get_if<InternalFlagNode>(dest)) {
            target = GetInternalFlag(flag->GetFlag());
        }

        code.AddLine(target + " = " + Visit(src) + ';');
        return {};
    }

    std::string LogicalAnd(Operation operation) {
        return GenerateBinaryInfix(operation, "&&", Type::Bool, Type::Bool, Type::Bool);
    }

    std::string LogicalOr(Operation operation) {
        return GenerateBinaryInfix(operation, "||", Type::Bool, Type::Bool, Type::Bool);
    }

    std::string LogicalXor(Operation operation) {
        return GenerateBinaryInfix(operation, "^^", Type::Bool, Type::Bool, Type::Bool);
    }

    std::string LogicalNegate(Operation operation) {
        return GenerateUnary(operation, "!", Type::Bool, Type::Bool, false);
    }

    std::string LogicalPick2(Operation operation) {
        const std::string pair = VisitOperand(operation, 0, Type::Bool2);
        return pair + '[' + VisitOperand(operation, 1, Type::Uint) + ']';
    }

    std::string LogicalAll2(Operation operation) {
        return GenerateUnary(operation, "all", Type::Bool, Type::Bool2);
    }

    std::string LogicalAny2(Operation operation) {
        return GenerateUnary(operation, "any", Type::Bool, Type::Bool2);
    }

    template <bool with_nan>
    std::string GenerateHalfComparison(Operation operation, std::string compare_op) {
        std::string comparison{GenerateBinaryCall(operation, compare_op, Type::Bool2,
                                                  Type::HalfFloat, Type::HalfFloat)};
        if constexpr (!with_nan) {
            return comparison;
        }
        return "halfFloatNanComparison(" + comparison + ", " +
               VisitOperand(operation, 0, Type::HalfFloat) + ", " +
               VisitOperand(operation, 1, Type::HalfFloat) + ')';
    }

    template <bool with_nan>
    std::string Logical2HLessThan(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "lessThan");
    }

    template <bool with_nan>
    std::string Logical2HEqual(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "equal");
    }

    template <bool with_nan>
    std::string Logical2HLessEqual(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "lessThanEqual");
    }

    template <bool with_nan>
    std::string Logical2HGreaterThan(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "greaterThan");
    }

    template <bool with_nan>
    std::string Logical2HNotEqual(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "notEqual");
    }

    template <bool with_nan>
    std::string Logical2HGreaterEqual(Operation operation) {
        return GenerateHalfComparison<with_nan>(operation, "greaterThanEqual");
    }

    std::string Texture(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        std::string expr = GenerateTexture(
            operation, "", {TextureAoffi{}, TextureArgument{Type::Float, meta->bias}});
        if (meta->sampler.IsShadow()) {
            expr = "vec4(" + expr + ')';
        }
        return expr + GetSwizzle(meta->element);
    }

    std::string TextureLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        std::string expr = GenerateTexture(
            operation, "Lod", {TextureArgument{Type::Float, meta->lod}, TextureAoffi{}});
        if (meta->sampler.IsShadow()) {
            expr = "vec4(" + expr + ')';
        }
        return expr + GetSwizzle(meta->element);
    }

    std::string TextureGather(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        const auto type = meta->sampler.IsShadow() ? Type::Float : Type::Int;
        return GenerateTexture(operation, "Gather",
                               {TextureArgument{type, meta->component}, TextureAoffi{}}) +
               GetSwizzle(meta->element);
    }

    std::string TextureQueryDimensions(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        const std::string sampler = GetSampler(meta->sampler);
        const std::string lod = VisitOperand(operation, 0, Type::Int);

        switch (meta->element) {
        case 0:
        case 1:
            return "itof(int(textureSize(" + sampler + ", " + lod + ')' +
                   GetSwizzle(meta->element) + "))";
        case 2:
            return "0";
        case 3:
            return "itof(textureQueryLevels(" + sampler + "))";
        }
        UNREACHABLE();
        return "0";
    }

    std::string TextureQueryLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        if (meta->element < 2) {
            return "itof(int((" + GenerateTexture(operation, "QueryLod", {}) + " * vec2(256))" +
                   GetSwizzle(meta->element) + "))";
        }
        return "0";
    }

    std::string TexelFetch(Operation operation) {
        constexpr std::array<const char*, 4> constructors = {"int", "ivec2", "ivec3", "ivec4"};
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);
        UNIMPLEMENTED_IF(meta->sampler.IsArray());
        const std::size_t count = operation.GetOperandsCount();

        std::string expr = "texelFetch(";
        expr += GetSampler(meta->sampler);
        expr += ", ";

        expr += constructors.at(operation.GetOperandsCount() - 1);
        expr += '(';
        for (std::size_t i = 0; i < count; ++i) {
            expr += VisitOperand(operation, i, Type::Int);
            const std::size_t next = i + 1;
            if (next == count)
                expr += ')';
            else if (next < count)
                expr += ", ";
        }
        if (meta->lod) {
            expr += ", ";
            expr += CastOperand(Visit(meta->lod), Type::Int);
        }
        expr += ')';

        return expr + GetSwizzle(meta->element);
    }

    std::string Branch(Operation operation) {
        const auto target = std::get_if<ImmediateNode>(operation[0]);
        UNIMPLEMENTED_IF(!target);

        code.AddLine(fmt::format("jmp_to = 0x{:x}u;", target->GetValue()));
        code.AddLine("break;");
        return {};
    }

    std::string PushFlowStack(Operation operation) {
        const auto target = std::get_if<ImmediateNode>(operation[0]);
        UNIMPLEMENTED_IF(!target);

        code.AddLine(fmt::format("flow_stack[flow_stack_top++] = 0x{:x}u;", target->GetValue()));
        return {};
    }

    std::string PopFlowStack(Operation operation) {
        code.AddLine("jmp_to = flow_stack[--flow_stack_top];");
        code.AddLine("break;");
        return {};
    }

    std::string Exit(Operation operation) {
        if (stage != ShaderStage::Fragment) {
            code.AddLine("return;");
            return {};
        }
        const auto& used_registers = ir.GetRegisters();
        const auto SafeGetRegister = [&](u32 reg) -> std::string {
            // TODO(Rodrigo): Replace with contains once C++20 releases
            if (used_registers.find(reg) != used_registers.end()) {
                return GetRegister(reg);
            }
            return "0.0f";
        };

        UNIMPLEMENTED_IF_MSG(header.ps.omap.sample_mask != 0, "Sample mask write is unimplemented");

        code.AddLine("if (alpha_test[0] != 0) {");
        ++code.scope;
        // We start on the register containing the alpha value in the first RT.
        u32 current_reg = 3;
        for (u32 render_target = 0; render_target < Maxwell::NumRenderTargets; ++render_target) {
            // TODO(Blinkhawk): verify the behavior of alpha testing on hardware when
            // multiple render targets are used.
            if (header.ps.IsColorComponentOutputEnabled(render_target, 0) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 1) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 2) ||
                header.ps.IsColorComponentOutputEnabled(render_target, 3)) {
                code.AddLine(
                    fmt::format("if (!AlphaFunc({})) discard;", SafeGetRegister(current_reg)));
                current_reg += 4;
            }
        }
        --code.scope;
        code.AddLine('}');

        // Write the color outputs using the data in the shader registers, disabled
        // rendertargets/components are skipped in the register assignment.
        current_reg = 0;
        for (u32 render_target = 0; render_target < Maxwell::NumRenderTargets; ++render_target) {
            // TODO(Subv): Figure out how dual-source blending is configured in the Switch.
            for (u32 component = 0; component < 4; ++component) {
                if (header.ps.IsColorComponentOutputEnabled(render_target, component)) {
                    code.AddLine(fmt::format("FragColor{}[{}] = {};", render_target, component,
                                             SafeGetRegister(current_reg)));
                    ++current_reg;
                }
            }
        }

        if (header.ps.omap.depth) {
            // The depth output is always 2 registers after the last color output, and current_reg
            // already contains one past the last color register.
            code.AddLine("gl_FragDepth = " + SafeGetRegister(current_reg + 1) + ';');
        }

        code.AddLine("return;");
        return {};
    }

    std::string Discard(Operation operation) {
        // Enclose "discard" in a conditional, so that GLSL compilation does not complain
        // about unexecuted instructions that may follow this.
        code.AddLine("if (true) {");
        ++code.scope;
        code.AddLine("discard;");
        --code.scope;
        code.AddLine("}");
        return {};
    }

    std::string EmitVertex(Operation operation) {
        ASSERT_MSG(stage == ShaderStage::Geometry,
                   "EmitVertex is expected to be used in a geometry shader.");

        // If a geometry shader is attached, it will always flip (it's the last stage before
        // fragment). For more info about flipping, refer to gl_shader_gen.cpp.
        code.AddLine("position.xy *= viewport_flip.xy;");
        code.AddLine("gl_Position = position;");
        code.AddLine("position.w = 1.0;");
        code.AddLine("EmitVertex();");
        return {};
    }

    std::string EndPrimitive(Operation operation) {
        ASSERT_MSG(stage == ShaderStage::Geometry,
                   "EndPrimitive is expected to be used in a geometry shader.");

        code.AddLine("EndPrimitive();");
        return {};
    }

    std::string YNegate(Operation operation) {
        // Config pack's third value is Y_NEGATE's state.
        return "uintBitsToFloat(config_pack[2])";
    }

    static constexpr OperationDecompilersArray operation_decompilers = {
        &GLSLDecompiler::Assign,

        &GLSLDecompiler::Select,

        &GLSLDecompiler::Add<Type::Float>,
        &GLSLDecompiler::Mul<Type::Float>,
        &GLSLDecompiler::Div<Type::Float>,
        &GLSLDecompiler::Fma<Type::Float>,
        &GLSLDecompiler::Negate<Type::Float>,
        &GLSLDecompiler::Absolute<Type::Float>,
        &GLSLDecompiler::FClamp,
        &GLSLDecompiler::Min<Type::Float>,
        &GLSLDecompiler::Max<Type::Float>,
        &GLSLDecompiler::FCos,
        &GLSLDecompiler::FSin,
        &GLSLDecompiler::FExp2,
        &GLSLDecompiler::FLog2,
        &GLSLDecompiler::FInverseSqrt,
        &GLSLDecompiler::FSqrt,
        &GLSLDecompiler::FRoundEven,
        &GLSLDecompiler::FFloor,
        &GLSLDecompiler::FCeil,
        &GLSLDecompiler::FTrunc,
        &GLSLDecompiler::FCastInteger<Type::Int>,
        &GLSLDecompiler::FCastInteger<Type::Uint>,

        &GLSLDecompiler::Add<Type::Int>,
        &GLSLDecompiler::Mul<Type::Int>,
        &GLSLDecompiler::Div<Type::Int>,
        &GLSLDecompiler::Negate<Type::Int>,
        &GLSLDecompiler::Absolute<Type::Int>,
        &GLSLDecompiler::Min<Type::Int>,
        &GLSLDecompiler::Max<Type::Int>,

        &GLSLDecompiler::ICastFloat,
        &GLSLDecompiler::ICastUnsigned,
        &GLSLDecompiler::LogicalShiftLeft<Type::Int>,
        &GLSLDecompiler::ILogicalShiftRight,
        &GLSLDecompiler::IArithmeticShiftRight,
        &GLSLDecompiler::BitwiseAnd<Type::Int>,
        &GLSLDecompiler::BitwiseOr<Type::Int>,
        &GLSLDecompiler::BitwiseXor<Type::Int>,
        &GLSLDecompiler::BitwiseNot<Type::Int>,
        &GLSLDecompiler::BitfieldInsert<Type::Int>,
        &GLSLDecompiler::BitfieldExtract<Type::Int>,
        &GLSLDecompiler::BitCount<Type::Int>,

        &GLSLDecompiler::Add<Type::Uint>,
        &GLSLDecompiler::Mul<Type::Uint>,
        &GLSLDecompiler::Div<Type::Uint>,
        &GLSLDecompiler::Min<Type::Uint>,
        &GLSLDecompiler::Max<Type::Uint>,
        &GLSLDecompiler::UCastFloat,
        &GLSLDecompiler::UCastSigned,
        &GLSLDecompiler::LogicalShiftLeft<Type::Uint>,
        &GLSLDecompiler::UShiftRight,
        &GLSLDecompiler::UShiftRight,
        &GLSLDecompiler::BitwiseAnd<Type::Uint>,
        &GLSLDecompiler::BitwiseOr<Type::Uint>,
        &GLSLDecompiler::BitwiseXor<Type::Uint>,
        &GLSLDecompiler::BitwiseNot<Type::Uint>,
        &GLSLDecompiler::BitfieldInsert<Type::Uint>,
        &GLSLDecompiler::BitfieldExtract<Type::Uint>,
        &GLSLDecompiler::BitCount<Type::Uint>,

        &GLSLDecompiler::Add<Type::HalfFloat>,
        &GLSLDecompiler::Mul<Type::HalfFloat>,
        &GLSLDecompiler::Fma<Type::HalfFloat>,
        &GLSLDecompiler::Absolute<Type::HalfFloat>,
        &GLSLDecompiler::HNegate,
        &GLSLDecompiler::HClamp,
        &GLSLDecompiler::HUnpack,
        &GLSLDecompiler::HMergeF32,
        &GLSLDecompiler::HMergeH0,
        &GLSLDecompiler::HMergeH1,
        &GLSLDecompiler::HPack2,

        &GLSLDecompiler::LogicalAssign,
        &GLSLDecompiler::LogicalAnd,
        &GLSLDecompiler::LogicalOr,
        &GLSLDecompiler::LogicalXor,
        &GLSLDecompiler::LogicalNegate,
        &GLSLDecompiler::LogicalPick2,
        &GLSLDecompiler::LogicalAll2,
        &GLSLDecompiler::LogicalAny2,

        &GLSLDecompiler::LogicalLessThan<Type::Float>,
        &GLSLDecompiler::LogicalEqual<Type::Float>,
        &GLSLDecompiler::LogicalLessEqual<Type::Float>,
        &GLSLDecompiler::LogicalGreaterThan<Type::Float>,
        &GLSLDecompiler::LogicalNotEqual<Type::Float>,
        &GLSLDecompiler::LogicalGreaterEqual<Type::Float>,
        &GLSLDecompiler::LogicalFIsNan,

        &GLSLDecompiler::LogicalLessThan<Type::Int>,
        &GLSLDecompiler::LogicalEqual<Type::Int>,
        &GLSLDecompiler::LogicalLessEqual<Type::Int>,
        &GLSLDecompiler::LogicalGreaterThan<Type::Int>,
        &GLSLDecompiler::LogicalNotEqual<Type::Int>,
        &GLSLDecompiler::LogicalGreaterEqual<Type::Int>,

        &GLSLDecompiler::LogicalLessThan<Type::Uint>,
        &GLSLDecompiler::LogicalEqual<Type::Uint>,
        &GLSLDecompiler::LogicalLessEqual<Type::Uint>,
        &GLSLDecompiler::LogicalGreaterThan<Type::Uint>,
        &GLSLDecompiler::LogicalNotEqual<Type::Uint>,
        &GLSLDecompiler::LogicalGreaterEqual<Type::Uint>,

        &GLSLDecompiler::Logical2HLessThan<false>,
        &GLSLDecompiler::Logical2HEqual<false>,
        &GLSLDecompiler::Logical2HLessEqual<false>,
        &GLSLDecompiler::Logical2HGreaterThan<false>,
        &GLSLDecompiler::Logical2HNotEqual<false>,
        &GLSLDecompiler::Logical2HGreaterEqual<false>,
        &GLSLDecompiler::Logical2HLessThan<true>,
        &GLSLDecompiler::Logical2HEqual<true>,
        &GLSLDecompiler::Logical2HLessEqual<true>,
        &GLSLDecompiler::Logical2HGreaterThan<true>,
        &GLSLDecompiler::Logical2HNotEqual<true>,
        &GLSLDecompiler::Logical2HGreaterEqual<true>,

        &GLSLDecompiler::Texture,
        &GLSLDecompiler::TextureLod,
        &GLSLDecompiler::TextureGather,
        &GLSLDecompiler::TextureQueryDimensions,
        &GLSLDecompiler::TextureQueryLod,
        &GLSLDecompiler::TexelFetch,

        &GLSLDecompiler::Branch,
        &GLSLDecompiler::PushFlowStack,
        &GLSLDecompiler::PopFlowStack,
        &GLSLDecompiler::Exit,
        &GLSLDecompiler::Discard,

        &GLSLDecompiler::EmitVertex,
        &GLSLDecompiler::EndPrimitive,

        &GLSLDecompiler::YNegate,
    };

    std::string GetRegister(u32 index) const {
        return GetDeclarationWithSuffix(index, "gpr");
    }

    std::string GetPredicate(Tegra::Shader::Pred pred) const {
        return GetDeclarationWithSuffix(static_cast<u32>(pred), "pred");
    }

    std::string GetInputAttribute(Attribute::Index attribute) const {
        return GetDeclarationWithSuffix(GetGenericAttributeIndex(attribute), "input_attr");
    }

    std::string GetOutputAttribute(Attribute::Index attribute) const {
        return GetDeclarationWithSuffix(GetGenericAttributeIndex(attribute), "output_attr");
    }

    std::string GetConstBuffer(u32 index) const {
        return GetDeclarationWithSuffix(index, "cbuf");
    }

    std::string GetGlobalMemory(const GlobalMemoryBase& descriptor) const {
        return fmt::format("gmem_{}_{}_{}", descriptor.cbuf_index, descriptor.cbuf_offset, suffix);
    }

    std::string GetGlobalMemoryBlock(const GlobalMemoryBase& descriptor) const {
        return fmt::format("gmem_block_{}_{}_{}", descriptor.cbuf_index, descriptor.cbuf_offset,
                           suffix);
    }

    std::string GetConstBufferBlock(u32 index) const {
        return GetDeclarationWithSuffix(index, "cbuf_block");
    }

    std::string GetLocalMemory() const {
        return "lmem_" + suffix;
    }

    std::string GetInternalFlag(InternalFlag flag) const {
        constexpr std::array<const char*, 4> InternalFlagNames = {"zero_flag", "sign_flag",
                                                                  "carry_flag", "overflow_flag"};
        const auto index = static_cast<u32>(flag);
        ASSERT(index < static_cast<u32>(InternalFlag::Amount));

        return std::string(InternalFlagNames[index]) + '_' + suffix;
    }

    std::string GetSampler(const Sampler& sampler) const {
        return GetDeclarationWithSuffix(static_cast<u32>(sampler.GetIndex()), "sampler");
    }

    std::string GetDeclarationWithSuffix(u32 index, const std::string& name) const {
        return name + '_' + std::to_string(index) + '_' + suffix;
    }

    u32 GetNumPhysicalInputAttributes() const {
        return stage == ShaderStage::Vertex ? GetNumPhysicalAttributes() : GetNumPhysicalVaryings();
    }

    u32 GetNumPhysicalAttributes() const {
        return std::min<u32>(device.GetMaxVertexAttributes(), Maxwell::NumVertexAttributes);
    }

    u32 GetNumPhysicalVaryings() const {
        return std::min<u32>(device.GetMaxVaryings() - GENERIC_VARYING_START_LOCATION,
                             Maxwell::NumVaryings);
    }

    const Device& device;
    const ShaderIR& ir;
    const ShaderStage stage;
    const std::string suffix;
    const Header header;

    ShaderWriter code;
};

} // Anonymous namespace

std::string GetCommonDeclarations() {
    const auto cbuf = std::to_string(MAX_CONSTBUFFER_ELEMENTS);
    return "#define MAX_CONSTBUFFER_ELEMENTS " + cbuf + "\n" +
           "#define ftoi floatBitsToInt\n"
           "#define ftou floatBitsToUint\n"
           "#define itof intBitsToFloat\n"
           "#define utof uintBitsToFloat\n\n"
           "float fromHalf2(vec2 pair) {\n"
           "    return utof(packHalf2x16(pair));\n"
           "}\n\n"
           "vec2 toHalf2(float value) {\n"
           "    return unpackHalf2x16(ftou(value));\n"
           "}\n\n"
           "bvec2 halfFloatNanComparison(bvec2 comparison, vec2 pair1, vec2 pair2) {\n"
           "    bvec2 is_nan1 = isnan(pair1);\n"
           "    bvec2 is_nan2 = isnan(pair2);\n"
           "    return bvec2(comparison.x || is_nan1.x || is_nan2.x, comparison.y || is_nan1.y || "
           "is_nan2.y);\n"
           "}\n";
}

ProgramResult Decompile(const Device& device, const ShaderIR& ir, Maxwell::ShaderStage stage,
                        const std::string& suffix) {
    GLSLDecompiler decompiler(device, ir, stage, suffix);
    decompiler.Decompile();
    return {decompiler.GetResult(), decompiler.GetShaderEntries()};
}

} // namespace OpenGL::GLShader
