// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"
#include "video_core/shader/shader_ir.h"

namespace OpenGL::GLShader {

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

enum : u32 { POSITION_VARYING_LOCATION = 0, GENERIC_VARYING_START_LOCATION = 1 };
constexpr u32 MAX_CONSTBUFFER_ELEMENTS =
    static_cast<u32>(RasterizerOpenGL::MaxConstbufferSize) / (4 * sizeof(float));
constexpr u32 MAX_GLOBALMEMORY_ELEMENTS =
    static_cast<u32>(RasterizerOpenGL::MaxGlobalMemorySize) / sizeof(float);

enum class Type { Bool, Bool2, Float, Int, Uint, HalfFloat };

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

    std::string GenerateTemporal() {
        std::string temporal = "tmp";
        temporal += std::to_string(temporal_index++);
        return temporal;
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
    u32 temporal_index = 1;
};

/// Generates code to use for a swizzle operation.
static std::string GetSwizzle(u32 elem) {
    ASSERT(elem <= 3);
    std::string swizzle = ".";
    swizzle += "xyzw"[elem];
    return swizzle;
}

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
        UNIMPLEMENTED_MSG("Unknown output topology: {}", static_cast<u32>(topology));
        return "points";
    }
}

/// Returns true if an object has to be treated as precise
static bool IsPrecise(Operation operand) {
    const auto& meta = operand.GetMeta();

    if (const auto arithmetic = std::get_if<MetaArithmetic>(&meta)) {
        return arithmetic->precise;
    }
    if (const auto half_arithmetic = std::get_if<MetaHalfArithmetic>(&meta)) {
        return half_arithmetic->precise;
    }
    return false;
}

static bool IsPrecise(Node node) {
    if (const auto operation = std::get_if<OperationNode>(node)) {
        return IsPrecise(*operation);
    }
    return false;
}

class GLSLDecompiler final {
public:
    explicit GLSLDecompiler(const ShaderIR& ir, ShaderStage stage, std::string suffix)
        : ir{ir}, stage{stage}, suffix{suffix}, header{ir.GetHeader()} {}

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
        for (const auto& gmem : ir.GetGlobalMemoryBases()) {
            entries.global_memory_entries.emplace_back(gmem.cbuf_index, gmem.cbuf_offset);
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
        std::string out;

        switch (attribute) {
        case AttributeUse::Constant:
            out += "flat ";
            break;
        case AttributeUse::ScreenLinear:
            out += "noperspective ";
            break;
        case AttributeUse::Perspective:
            // Default, Smooth
            break;
        default:
            LOG_CRITICAL(HW_GPU, "Unused attribute being fetched");
            UNREACHABLE();
        }
        return out;
    }

    void DeclareInputAttributes() {
        const auto& attributes = ir.GetInputAttributes();
        for (const auto element : attributes) {
            const Attribute::Index index = element.first;
            if (index < Attribute::Index::Attribute_0 || index > Attribute::Index::Attribute_31) {
                // Skip when it's not a generic attribute
                continue;
            }

            // TODO(bunnei): Use proper number of elements for these
            u32 idx = static_cast<u32>(index) - static_cast<u32>(Attribute::Index::Attribute_0);
            if (stage != ShaderStage::Vertex) {
                // If inputs are varyings, add an offset
                idx += GENERIC_VARYING_START_LOCATION;
            }

            std::string attr = GetInputAttribute(index);
            if (stage == ShaderStage::Geometry) {
                attr = "gs_" + attr + "[]";
            }
            std::string suffix;
            if (stage == ShaderStage::Fragment) {
                const auto input_mode =
                    header.ps.GetAttributeUse(idx - GENERIC_VARYING_START_LOCATION);
                suffix = GetInputFlags(input_mode);
            }
            code.AddLine("layout (location = " + std::to_string(idx) + ") " + suffix + "in vec4 " +
                         attr + ';');
        }
        if (!attributes.empty())
            code.AddNewLine();
    }

    void DeclareOutputAttributes() {
        const auto& attributes = ir.GetOutputAttributes();
        for (const auto index : attributes) {
            if (index < Attribute::Index::Attribute_0 || index > Attribute::Index::Attribute_31) {
                // Skip when it's not a generic attribute
                continue;
            }
            // TODO(bunnei): Use proper number of elements for these
            const auto idx = static_cast<u32>(index) -
                             static_cast<u32>(Attribute::Index::Attribute_0) +
                             GENERIC_VARYING_START_LOCATION;
            code.AddLine("layout (location = " + std::to_string(idx) + ") out vec4 " +
                         GetOutputAttribute(index) + ';');
        }
        if (!attributes.empty())
            code.AddNewLine();
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
        for (const auto& entry : ir.GetGlobalMemoryBases()) {
            const std::string binding =
                fmt::format("GMEM_BINDING_{}_{}", entry.cbuf_index, entry.cbuf_offset);
            code.AddLine("layout (std430, binding = " + binding + ") buffer " +
                         GetGlobalMemoryBlock(entry) + " {");
            code.AddLine("    float " + GetGlobalMemory(entry) + "[MAX_GLOBALMEMORY_ELEMENTS];");
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
            const auto decompiler = operation_decompilers[operation_index];
            if (decompiler == nullptr) {
                UNREACHABLE_MSG("Operation decompiler {} not defined", operation_index);
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
            const auto attribute = abuf->GetIndex();
            const auto element = abuf->GetElement();

            const auto GeometryPass = [&](const std::string& name) {
                if (stage == ShaderStage::Geometry && abuf->GetBuffer()) {
                    // TODO(Rodrigo): Guard geometry inputs against out of bound reads. Some games
                    // set an 0x80000000 index for those and the shader fails to build. Find out why
                    // this happens and what's its intent.
                    return "gs_" + name + "[ftou(" + Visit(abuf->GetBuffer()) +
                           ") % MAX_VERTEX_INPUT]";
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
                if (attribute >= Attribute::Index::Attribute_0 &&
                    attribute <= Attribute::Index::Attribute_31) {
                    return GeometryPass(GetInputAttribute(attribute)) + GetSwizzle(element);
                }
                break;
            }
            UNIMPLEMENTED_MSG("Unhandled input attribute: {}", static_cast<u32>(attribute));

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
                const std::string final_offset = code.GenerateTemporal();
                code.AddLine("uint " + final_offset + " = (ftou(" + Visit(offset) + ") / 4) & " +
                             std::to_string(MAX_CONSTBUFFER_ELEMENTS - 1) + ';');
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

    std::string ApplyPrecise(Operation operation, const std::string& value) {
        if (!IsPrecise(operation)) {
            return value;
        }
        // There's a bug in NVidia's proprietary drivers that makes precise fail on fragment shaders
        const std::string precise = stage != ShaderStage::Fragment ? "precise " : "";

        const std::string temporal = code.GenerateTemporal();
        code.AddLine(precise + "float " + temporal + " = " + value + ';');
        return temporal;
    }

    std::string VisitOperand(Operation operation, std::size_t operand_index) {
        const auto& operand = operation[operand_index];
        const bool parent_precise = IsPrecise(operation);
        const bool child_precise = IsPrecise(operand);
        const bool child_trivial = !std::holds_alternative<OperationNode>(*operand);
        if (!parent_precise || child_precise || child_trivial) {
            return Visit(operand);
        }

        const std::string temporal = code.GenerateTemporal();
        code.AddLine("float " + temporal + " = " + Visit(operand) + ';');
        return temporal;
    }

    std::string VisitOperand(Operation operation, std::size_t operand_index, Type type) {
        std::string value = VisitOperand(operation, operand_index);
        switch (type) {
        case Type::HalfFloat: {
            const auto half_meta = std::get_if<MetaHalfArithmetic>(&operation.GetMeta());
            if (!half_meta) {
                value = "toHalf2(" + value + ')';
            }

            switch (half_meta->types.at(operand_index)) {
            case Tegra::Shader::HalfType::H0_H1:
                return "toHalf2(" + value + ')';
            case Tegra::Shader::HalfType::F32:
                return "vec2(" + value + ')';
            case Tegra::Shader::HalfType::H0_H0:
                return "vec2(toHalf2(" + value + ")[0])";
            case Tegra::Shader::HalfType::H1_H1:
                return "vec2(toHalf2(" + value + ")[1])";
            }
        }
        default:
            return CastOperand(value, type);
        }
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
            // Can't be handled as a stand-alone value
            UNREACHABLE();
            return value;
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

    std::string GenerateTexture(Operation operation, const std::string& func,
                                bool is_extra_int = false) {
        constexpr std::array<const char*, 4> coord_constructors = {"float", "vec2", "vec3", "vec4"};

        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        const std::size_t count = operation.GetOperandsCount();
        const bool has_array = meta->sampler.IsArray();
        const bool has_shadow = meta->sampler.IsShadow();

        std::string expr = func;
        expr += '(';
        expr += GetSampler(meta->sampler);
        expr += ", ";

        expr += coord_constructors.at(count + (has_array ? 1 : 0) + (has_shadow ? 1 : 0) - 1);
        expr += '(';
        for (std::size_t i = 0; i < count; ++i) {
            expr += Visit(operation[i]);

            const std::size_t next = i + 1;
            if (next < count || has_array || has_shadow)
                expr += ", ";
        }
        if (has_array) {
            expr += "float(ftoi(" + Visit(meta->array) + "))";
        }
        if (has_shadow) {
            if (has_array)
                expr += ", ";
            expr += Visit(meta->depth_compare);
        }
        expr += ')';

        for (const Node extra : meta->extras) {
            expr += ", ";
            if (is_extra_int) {
                if (const auto immediate = std::get_if<ImmediateNode>(extra)) {
                    // Inline the string as an immediate integer in GLSL (some extra arguments are
                    // required to be constant)
                    expr += std::to_string(static_cast<s32>(immediate->GetValue()));
                } else {
                    expr += "ftoi(" + Visit(extra) + ')';
                }
            } else {
                expr += Visit(extra);
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
                    if (attribute >= Attribute::Index::Attribute_0 &&
                        attribute <= Attribute::Index::Attribute_31) {
                        return GetOutputAttribute(attribute) + GetSwizzle(abuf->GetElement());
                    }
                    UNIMPLEMENTED_MSG("Unhandled output attribute: {}",
                                      static_cast<u32>(attribute));
                    return "0";
                }
            }();

        } else if (const auto lmem = std::get_if<LmemNode>(dest)) {
            target = GetLocalMemory() + "[ftou(" + Visit(lmem->GetAddress()) + ") / 4]";

        } else {
            UNREACHABLE_MSG("Assign called without a proper target");
        }

        code.AddLine(target + " = " + Visit(src) + ';');
        return {};
    }

    std::string Composite(Operation operation) {
        std::string value = "vec4(";
        for (std::size_t i = 0; i < 4; ++i) {
            value += Visit(operation[i]);
            if (i < 3)
                value += ", ";
        }
        value += ')';
        return value;
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

    std::string HMergeF32(Operation operation) {
        return "float(toHalf2(" + Visit(operation[0]) + ")[0])";
    }

    std::string HMergeH0(Operation operation) {
        return "fromHalf2(vec2(toHalf2(" + Visit(operation[0]) + ")[1], toHalf2(" +
               Visit(operation[1]) + ")[0]))";
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

    std::string Logical2HLessThan(Operation operation) {
        return GenerateBinaryCall(operation, "lessThan", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Logical2HEqual(Operation operation) {
        return GenerateBinaryCall(operation, "equal", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Logical2HLessEqual(Operation operation) {
        return GenerateBinaryCall(operation, "lessThanEqual", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Logical2HGreaterThan(Operation operation) {
        return GenerateBinaryCall(operation, "greaterThan", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Logical2HNotEqual(Operation operation) {
        return GenerateBinaryCall(operation, "notEqual", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Logical2HGreaterEqual(Operation operation) {
        return GenerateBinaryCall(operation, "greaterThanEqual", Type::Bool2, Type::HalfFloat,
                                  Type::HalfFloat);
    }

    std::string Texture(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        std::string expr = GenerateTexture(operation, "texture");
        if (meta->sampler.IsShadow()) {
            expr = "vec4(" + expr + ')';
        }
        return expr + GetSwizzle(meta->element);
    }

    std::string TextureLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        std::string expr = GenerateTexture(operation, "textureLod");
        if (meta->sampler.IsShadow()) {
            expr = "vec4(" + expr + ')';
        }
        return expr + GetSwizzle(meta->element);
    }

    std::string TextureGather(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        return GenerateTexture(operation, "textureGather", !meta->sampler.IsShadow()) +
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
            return "textureSize(" + sampler + ", " + lod + ')' + GetSwizzle(meta->element);
        case 2:
            return "0";
        case 3:
            return "textureQueryLevels(" + sampler + ')';
        }
        UNREACHABLE();
        return "0";
    }

    std::string TextureQueryLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        if (meta->element < 2) {
            return "itof(int((" + GenerateTexture(operation, "textureQueryLod") + " * vec2(256))" +
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
        for (std::size_t i = 0; i < meta->extras.size(); ++i) {
            expr += ", ";
            expr += CastOperand(Visit(meta->extras.at(i)), Type::Int);
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

        &GLSLDecompiler::Logical2HLessThan,
        &GLSLDecompiler::Logical2HEqual,
        &GLSLDecompiler::Logical2HLessEqual,
        &GLSLDecompiler::Logical2HGreaterThan,
        &GLSLDecompiler::Logical2HNotEqual,
        &GLSLDecompiler::Logical2HGreaterEqual,

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
        const auto index{static_cast<u32>(attribute) -
                         static_cast<u32>(Attribute::Index::Attribute_0)};
        return GetDeclarationWithSuffix(index, "input_attr");
    }

    std::string GetOutputAttribute(Attribute::Index attribute) const {
        const auto index{static_cast<u32>(attribute) -
                         static_cast<u32>(Attribute::Index::Attribute_0)};
        return GetDeclarationWithSuffix(index, "output_attr");
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

    const ShaderIR& ir;
    const ShaderStage stage;
    const std::string suffix;
    const Header header;

    ShaderWriter code;
};

std::string GetCommonDeclarations() {
    const auto cbuf = std::to_string(MAX_CONSTBUFFER_ELEMENTS);
    const auto gmem = std::to_string(MAX_GLOBALMEMORY_ELEMENTS);
    return "#define MAX_CONSTBUFFER_ELEMENTS " + cbuf + "\n" +
           "#define MAX_GLOBALMEMORY_ELEMENTS " + gmem + "\n" +
           "#define ftoi floatBitsToInt\n"
           "#define ftou floatBitsToUint\n"
           "#define itof intBitsToFloat\n"
           "#define utof uintBitsToFloat\n\n"
           "float fromHalf2(vec2 pair) {\n"
           "    return utof(packHalf2x16(pair));\n"
           "}\n\n"
           "vec2 toHalf2(float value) {\n"
           "    return unpackHalf2x16(ftou(value));\n"
           "}\n";
}

ProgramResult Decompile(const ShaderIR& ir, Maxwell::ShaderStage stage, const std::string& suffix) {
    GLSLDecompiler decompiler(ir, stage, suffix);
    decompiler.Decompile();
    return {decompiler.GetResult(), decompiler.GetShaderEntries()};
}

} // namespace OpenGL::GLShader
