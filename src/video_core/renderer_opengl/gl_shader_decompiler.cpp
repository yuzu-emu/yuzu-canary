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
#include "common/logging/log.h"
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

using namespace std::string_literals;
using namespace VideoCommon::Shader;

using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using Operation = const OperationNode&;

enum class Type { Bool, Bool2, Float, Int, Uint, HalfFloat };

struct TextureAoffi {};
using TextureArgument = std::pair<Type, Node>;
using TextureIR = std::variant<TextureAoffi, TextureArgument>;

constexpr u32 MAX_CONSTBUFFER_ELEMENTS =
    static_cast<u32>(Maxwell::MaxConstBufferSize) / (4 * sizeof(float));

class ShaderWriter {
public:
    void AddExpression(std::string_view text) {
        DEBUG_ASSERT(scope >= 0);
        if (!text.empty()) {
            AppendIndentation();
        }
        shader_source += text;
    }

    // Forwards all arguments directly to libfmt.
    // Note that all formatting requirements for fmt must be
    // obeyed when using this function. (e.g. {{ must be used
    // printing the character '{' is desirable. Ditto for }} and '}',
    // etc).
    template <typename... Args>
    void AddLine(std::string_view text, Args&&... args) {
        AddExpression(fmt::format(text, std::forward<Args>(args)...));
        AddNewLine();
    }

    void AddNewLine() {
        DEBUG_ASSERT(scope >= 0);
        shader_source += '\n';
    }

    std::string GenerateTemporary() {
        return fmt::format("tmp{}", temporary_index++);
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
constexpr const char* GetSwizzle(u32 element) {
    constexpr std::array<const char*, 4> swizzle = {".x", ".y", ".z", ".w"};
    return swizzle.at(element);
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

bool IsPrecise(const Node& node) {
    if (const auto operation = std::get_if<OperationNode>(&*node)) {
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

constexpr const char* GetFlowStackPrefix(MetaStackClass stack) {
    switch (stack) {
    case MetaStackClass::Ssy:
        return "ssy";
    case MetaStackClass::Pbk:
        return "pbk";
    }
    return {};
}

std::string FlowStackName(MetaStackClass stack) {
    return fmt::format("{}_flow_stack", GetFlowStackPrefix(stack));
}

std::string FlowStackTopName(MetaStackClass stack) {
    return fmt::format("{}_flow_stack_top", GetFlowStackPrefix(stack));
}

constexpr bool IsVertexShader(ProgramType stage) {
    return stage == ProgramType::VertexA || stage == ProgramType::VertexB;
}

class GLSLDecompiler final {
public:
    explicit GLSLDecompiler(const Device& device, const ShaderIR& ir, ProgramType stage,
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
        DeclareImages();

        code.AddLine("void execute_{}() {{", suffix);
        ++code.scope;

        // VM's program counter
        const auto first_address = ir.GetBasicBlocks().begin()->first;
        code.AddLine("uint jmp_to = {}u;", first_address);

        // TODO(Subv): Figure out the actual depth of the flow stack, for now it seems
        // unlikely that shaders will use 20 nested SSYs and PBKs.
        if (!ir.IsFlowStackDisabled()) {
            constexpr u32 FLOW_STACK_SIZE = 20;
            for (const auto stack : std::array{MetaStackClass::Ssy, MetaStackClass::Pbk}) {
                code.AddLine("uint {}[{}];", FlowStackName(stack), FLOW_STACK_SIZE);
                code.AddLine("uint {} = 0u;", FlowStackTopName(stack));
            }
        }

        code.AddLine("while (true) {{");
        ++code.scope;

        code.AddLine("switch (jmp_to) {{");

        for (const auto& pair : ir.GetBasicBlocks()) {
            const auto [address, bb] = pair;
            code.AddLine("case 0x{:x}u: {{", address);
            ++code.scope;

            VisitBlock(bb);

            --code.scope;
            code.AddLine("}}");
        }

        code.AddLine("default: return;");
        code.AddLine("}}");

        for (std::size_t i = 0; i < 2; ++i) {
            --code.scope;
            code.AddLine("}}");
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
        for (const auto& image : ir.GetImages()) {
            entries.images.emplace_back(image);
        }
        for (const auto& gmem_pair : ir.GetGlobalMemory()) {
            const auto& [base, usage] = gmem_pair;
            entries.global_memory_entries.emplace_back(base.cbuf_index, base.cbuf_offset,
                                                       usage.is_read, usage.is_written);
        }
        entries.clip_distances = ir.GetClipDistances();
        entries.shader_viewport_layer_array =
            IsVertexShader(stage) && (ir.UsesLayer() || ir.UsesViewportIndex());
        entries.shader_length = ir.GetLength();
        return entries;
    }

private:
    void DeclareVertex() {
        if (!IsVertexShader(stage))
            return;

        DeclareVertexRedeclarations();
    }

    void DeclareGeometry() {
        if (stage != ProgramType::Geometry) {
            return;
        }

        const auto topology = GetTopologyName(header.common3.output_topology);
        const auto max_vertices = header.common4.max_output_vertices.Value();
        code.AddLine("layout ({}, max_vertices = {}) out;", topology, max_vertices);
        code.AddNewLine();

        code.AddLine("in gl_PerVertex {{");
        ++code.scope;
        code.AddLine("vec4 gl_Position;");
        --code.scope;
        code.AddLine("}} gl_in[];");

        DeclareVertexRedeclarations();
    }

    void DeclareVertexRedeclarations() {
        code.AddLine("out gl_PerVertex {{");
        ++code.scope;

        code.AddLine("vec4 gl_Position;");

        for (const auto attribute : ir.GetOutputAttributes()) {
            if (attribute == Attribute::Index::ClipDistances0123 ||
                attribute == Attribute::Index::ClipDistances4567) {
                code.AddLine("float gl_ClipDistance[];");
                break;
            }
        }
        if (!IsVertexShader(stage) || device.HasVertexViewportLayer()) {
            if (ir.UsesLayer()) {
                code.AddLine("int gl_Layer;");
            }
            if (ir.UsesViewportIndex()) {
                code.AddLine("int gl_ViewportIndex;");
            }
        } else if ((ir.UsesLayer() || ir.UsesViewportIndex()) && IsVertexShader(stage) &&
                   !device.HasVertexViewportLayer()) {
            LOG_ERROR(
                Render_OpenGL,
                "GL_ARB_shader_viewport_layer_array is not available and its required by a shader");
        }

        if (ir.UsesPointSize()) {
            code.AddLine("float gl_PointSize;");
        }

        --code.scope;
        code.AddLine("}};");
        code.AddNewLine();
    }

    void DeclareRegisters() {
        const auto& registers = ir.GetRegisters();
        for (const u32 gpr : registers) {
            code.AddLine("float {} = 0;", GetRegister(gpr));
        }
        if (!registers.empty()) {
            code.AddNewLine();
        }
    }

    void DeclarePredicates() {
        const auto& predicates = ir.GetPredicates();
        for (const auto pred : predicates) {
            code.AddLine("bool {} = false;", GetPredicate(pred));
        }
        if (!predicates.empty()) {
            code.AddNewLine();
        }
    }

    void DeclareLocalMemory() {
        // TODO(Rodrigo): Unstub kernel local memory size and pass it from a register at
        // specialization time.
        const u64 local_memory_size =
            stage == ProgramType::Compute ? 0x400 : header.GetLocalMemorySize();
        if (local_memory_size == 0) {
            return;
        }
        const auto element_count = Common::AlignUp(local_memory_size, 4) / 4;
        code.AddLine("float {}[{}];", GetLocalMemory(), element_count);
        code.AddNewLine();
    }

    void DeclareInternalFlags() {
        for (u32 flag = 0; flag < static_cast<u32>(InternalFlag::Amount); flag++) {
            const auto flag_code = static_cast<InternalFlag>(flag);
            code.AddLine("bool {} = false;", GetInternalFlag(flag_code));
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
        if (!attributes.empty()) {
            code.AddNewLine();
        }
    }

    void DeclareInputAttribute(Attribute::Index index, bool skip_unused) {
        const u32 location{GetGenericAttributeIndex(index)};

        std::string name{GetInputAttribute(index)};
        if (stage == ProgramType::Geometry) {
            name = "gs_" + name + "[]";
        }

        std::string suffix;
        if (stage == ProgramType::Fragment) {
            const auto input_mode{header.ps.GetAttributeUse(location)};
            if (skip_unused && input_mode == AttributeUse::Unused) {
                return;
            }
            suffix = GetInputFlags(input_mode);
        }

        code.AddLine("layout (location = {}) {} in vec4 {};", location, suffix, name);
    }

    void DeclareOutputAttributes() {
        if (ir.HasPhysicalAttributes() && stage != ProgramType::Fragment) {
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
        if (!attributes.empty()) {
            code.AddNewLine();
        }
    }

    void DeclareOutputAttribute(Attribute::Index index) {
        const u32 location{GetGenericAttributeIndex(index)};
        code.AddLine("layout (location = {}) out vec4 {};", location, GetOutputAttribute(index));
    }

    void DeclareConstantBuffers() {
        for (const auto& entry : ir.GetConstantBuffers()) {
            const auto [index, size] = entry;
            code.AddLine("layout (std140, binding = CBUF_BINDING_{}) uniform {} {{", index,
                         GetConstBufferBlock(index));
            code.AddLine("    vec4 {}[MAX_CONSTBUFFER_ELEMENTS];", GetConstBuffer(index));
            code.AddLine("}};");
            code.AddNewLine();
        }
    }

    void DeclareGlobalMemory() {
        for (const auto& gmem : ir.GetGlobalMemory()) {
            const auto& [base, usage] = gmem;

            // Since we don't know how the shader will use the shader, hint the driver to disable as
            // much optimizations as possible
            std::string qualifier = "coherent volatile";
            if (usage.is_read && !usage.is_written) {
                qualifier += " readonly";
            } else if (usage.is_written && !usage.is_read) {
                qualifier += " writeonly";
            }

            code.AddLine("layout (std430, binding = GMEM_BINDING_{}_{}) {} buffer {} {{",
                         base.cbuf_index, base.cbuf_offset, qualifier, GetGlobalMemoryBlock(base));
            code.AddLine("    float {}[];", GetGlobalMemory(base));
            code.AddLine("}};");
            code.AddNewLine();
        }
    }

    void DeclareSamplers() {
        const auto& samplers = ir.GetSamplers();
        for (const auto& sampler : samplers) {
            const std::string name{GetSampler(sampler)};
            const std::string description{"layout (binding = SAMPLER_BINDING_" +
                                          std::to_string(sampler.GetIndex()) + ") uniform"};
            std::string sampler_type = [&]() {
                switch (sampler.GetType()) {
                case Tegra::Shader::TextureType::Texture1D:
                    // Special cased, read below.
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
            if (sampler.IsArray()) {
                sampler_type += "Array";
            }
            if (sampler.IsShadow()) {
                sampler_type += "Shadow";
            }

            if (sampler.GetType() == Tegra::Shader::TextureType::Texture1D) {
                // 1D textures can be aliased to texture buffers, hide the declarations behind a
                // preprocessor flag and use one or the other from the GPU state. This has to be
                // done because shaders don't have enough information to determine the texture type.
                EmitIfdefIsBuffer(sampler);
                code.AddLine("{} samplerBuffer {};", description, name);
                code.AddLine("#else");
                code.AddLine("{} {} {};", description, sampler_type, name);
                code.AddLine("#endif");
            } else {
                // The other texture types (2D, 3D and cubes) don't have this issue.
                code.AddLine("{} {} {};", description, sampler_type, name);
            }
        }
        if (!samplers.empty()) {
            code.AddNewLine();
        }
    }

    void DeclarePhysicalAttributeReader() {
        if (!ir.HasPhysicalAttributes()) {
            return;
        }
        code.AddLine("float readPhysicalAttribute(uint physical_address) {{");
        ++code.scope;
        code.AddLine("switch (physical_address) {{");

        // Just declare generic attributes for now.
        const auto num_attributes{static_cast<u32>(GetNumPhysicalInputAttributes())};
        for (u32 index = 0; index < num_attributes; ++index) {
            const auto attribute{ToGenericAttribute(index)};
            for (u32 element = 0; element < 4; ++element) {
                constexpr u32 generic_base{0x80};
                constexpr u32 generic_stride{16};
                constexpr u32 element_stride{4};
                const u32 address{generic_base + index * generic_stride + element * element_stride};

                const bool declared{stage != ProgramType::Fragment ||
                                    header.ps.GetAttributeUse(index) != AttributeUse::Unused};
                const std::string value{declared ? ReadAttribute(attribute, element) : "0"};
                code.AddLine("case 0x{:x}: return {};", address, value);
            }
        }

        code.AddLine("default: return 0;");

        code.AddLine("}}");
        --code.scope;
        code.AddLine("}}");
        code.AddNewLine();
    }

    void DeclareImages() {
        const auto& images{ir.GetImages()};
        for (const auto& image : images) {
            const std::string image_type = [&]() {
                switch (image.GetType()) {
                case Tegra::Shader::ImageType::Texture1D:
                    return "image1D";
                case Tegra::Shader::ImageType::TextureBuffer:
                    return "bufferImage";
                case Tegra::Shader::ImageType::Texture1DArray:
                    return "image1DArray";
                case Tegra::Shader::ImageType::Texture2D:
                    return "image2D";
                case Tegra::Shader::ImageType::Texture2DArray:
                    return "image2DArray";
                case Tegra::Shader::ImageType::Texture3D:
                    return "image3D";
                default:
                    UNREACHABLE();
                    return "image1D";
                }
            }();
            code.AddLine("layout (binding = IMAGE_BINDING_{}) coherent volatile writeonly uniform "
                         "{} {};",
                         image.GetIndex(), image_type, GetImage(image));
        }
        if (!images.empty()) {
            code.AddNewLine();
        }
    }

    void VisitBlock(const NodeBlock& bb) {
        for (const auto& node : bb) {
            if (const std::string expr = Visit(node); !expr.empty()) {
                code.AddLine(expr);
            }
        }
    }

    std::string Visit(const Node& node) {
        if (const auto operation = std::get_if<OperationNode>(&*node)) {
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
        }

        if (const auto gpr = std::get_if<GprNode>(&*node)) {
            const u32 index = gpr->GetIndex();
            if (index == Register::ZeroIndex) {
                return "0";
            }
            return GetRegister(index);
        }

        if (const auto immediate = std::get_if<ImmediateNode>(&*node)) {
            const u32 value = immediate->GetValue();
            if (value < 10) {
                // For eyecandy avoid using hex numbers on single digits
                return fmt::format("utof({}u)", immediate->GetValue());
            }
            return fmt::format("utof(0x{:x}u)", immediate->GetValue());
        }

        if (const auto predicate = std::get_if<PredicateNode>(&*node)) {
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
                return fmt::format("!({})", value);
            }
            return value;
        }

        if (const auto abuf = std::get_if<AbufNode>(&*node)) {
            UNIMPLEMENTED_IF_MSG(abuf->IsPhysicalBuffer() && stage == ProgramType::Geometry,
                                 "Physical attributes in geometry shaders are not implemented");
            if (abuf->IsPhysicalBuffer()) {
                return fmt::format("readPhysicalAttribute(ftou({}))",
                                   Visit(abuf->GetPhysicalAddress()));
            }
            return ReadAttribute(abuf->GetIndex(), abuf->GetElement(), abuf->GetBuffer());
        }

        if (const auto cbuf = std::get_if<CbufNode>(&*node)) {
            const Node offset = cbuf->GetOffset();
            if (const auto immediate = std::get_if<ImmediateNode>(&*offset)) {
                // Direct access
                const u32 offset_imm = immediate->GetValue();
                ASSERT_MSG(offset_imm % 4 == 0, "Unaligned cbuf direct access");
                return fmt::format("{}[{}][{}]", GetConstBuffer(cbuf->GetIndex()),
                                   offset_imm / (4 * 4), (offset_imm / 4) % 4);
            }

            if (std::holds_alternative<OperationNode>(*offset)) {
                // Indirect access
                const std::string final_offset = code.GenerateTemporary();
                code.AddLine("uint {} = ftou({}) >> 2;", final_offset, Visit(offset));

                if (!device.HasComponentIndexingBug()) {
                    return fmt::format("{}[{} >> 2][{} & 3]", GetConstBuffer(cbuf->GetIndex()),
                                       final_offset, final_offset);
                }

                // AMD's proprietary GLSL compiler emits ill code for variable component access.
                // To bypass this driver bug generate 4 ifs, one per each component.
                const std::string pack = code.GenerateTemporary();
                code.AddLine("vec4 {} = {}[{} >> 2];", pack, GetConstBuffer(cbuf->GetIndex()),
                             final_offset);

                const std::string result = code.GenerateTemporary();
                code.AddLine("float {};", result);
                for (u32 swizzle = 0; swizzle < 4; ++swizzle) {
                    code.AddLine("if (({} & 3) == {}) {} = {}{};", final_offset, swizzle, result,
                                 pack, GetSwizzle(swizzle));
                }
                return result;
            }

            UNREACHABLE_MSG("Unmanaged offset node type");
        }

        if (const auto gmem = std::get_if<GmemNode>(&*node)) {
            const std::string real = Visit(gmem->GetRealAddress());
            const std::string base = Visit(gmem->GetBaseAddress());
            const std::string final_offset = fmt::format("(ftou({}) - ftou({})) / 4", real, base);
            return fmt::format("{}[{}]", GetGlobalMemory(gmem->GetDescriptor()), final_offset);
        }

        if (const auto lmem = std::get_if<LmemNode>(&*node)) {
            if (stage == ProgramType::Compute) {
                LOG_WARNING(Render_OpenGL, "Local memory is stubbed on compute shaders");
            }
            return fmt::format("{}[ftou({}) / 4]", GetLocalMemory(), Visit(lmem->GetAddress()));
        }

        if (const auto internal_flag = std::get_if<InternalFlagNode>(&*node)) {
            return GetInternalFlag(internal_flag->GetFlag());
        }

        if (const auto conditional = std::get_if<ConditionalNode>(&*node)) {
            // It's invalid to call conditional on nested nodes, use an operation instead
            code.AddLine("if ({}) {{", Visit(conditional->GetCondition()));
            ++code.scope;

            VisitBlock(conditional->GetCode());

            --code.scope;
            code.AddLine("}}");
            return {};
        }

        if (const auto comment = std::get_if<CommentNode>(&*node)) {
            return "// " + comment->GetText();
        }

        UNREACHABLE();
        return {};
    }

    std::string ReadAttribute(Attribute::Index attribute, u32 element, const Node& buffer = {}) {
        const auto GeometryPass = [&](std::string_view name) {
            if (stage == ProgramType::Geometry && buffer) {
                // TODO(Rodrigo): Guard geometry inputs against out of bound reads. Some games
                // set an 0x80000000 index for those and the shader fails to build. Find out why
                // this happens and what's its intent.
                return fmt::format("gs_{}[ftou({}) % MAX_VERTEX_INPUT]", name, Visit(buffer));
            }
            return std::string(name);
        };

        switch (attribute) {
        case Attribute::Index::Position:
            switch (stage) {
            case ProgramType::Geometry:
                return fmt::format("gl_in[ftou({})].gl_Position{}", Visit(buffer),
                                   GetSwizzle(element));
            case ProgramType::Fragment:
                return element == 3 ? "1.0f" : ("gl_FragCoord"s + GetSwizzle(element));
            default:
                UNREACHABLE();
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
            ASSERT(IsVertexShader(stage));
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
            ASSERT(stage == ProgramType::Fragment);
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
        const std::string precise = stage != ProgramType::Fragment ? "precise " : "";

        const std::string temporary = code.GenerateTemporary();
        code.AddLine("{}float {} = {};", precise, temporary, value);
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
        code.AddLine("float {} = {};", temporary, Visit(operand));
        return temporary;
    }

    std::string VisitOperand(Operation operation, std::size_t operand_index, Type type) {
        return CastOperand(VisitOperand(operation, operand_index), type);
    }

    std::optional<std::pair<std::string, bool>> GetOutputAttribute(const AbufNode* abuf) {
        switch (const auto attribute = abuf->GetIndex()) {
        case Attribute::Index::Position:
            return std::make_pair("gl_Position"s + GetSwizzle(abuf->GetElement()), false);
        case Attribute::Index::LayerViewportPointSize:
            switch (abuf->GetElement()) {
            case 0:
                UNIMPLEMENTED();
                return {};
            case 1:
                if (IsVertexShader(stage) && !device.HasVertexViewportLayer()) {
                    return {};
                }
                return std::make_pair("gl_Layer", true);
            case 2:
                if (IsVertexShader(stage) && !device.HasVertexViewportLayer()) {
                    return {};
                }
                return std::make_pair("gl_ViewportIndex", true);
            case 3:
                UNIMPLEMENTED_MSG("Requires some state changes for gl_PointSize to work in shader");
                return std::make_pair("gl_PointSize", false);
            }
            return {};
        case Attribute::Index::ClipDistances0123:
            return std::make_pair(fmt::format("gl_ClipDistance[{}]", abuf->GetElement()), false);
        case Attribute::Index::ClipDistances4567:
            return std::make_pair(fmt::format("gl_ClipDistance[{}]", abuf->GetElement() + 4),
                                  false);
        default:
            if (IsGenericAttribute(attribute)) {
                return std::make_pair(
                    GetOutputAttribute(attribute) + GetSwizzle(abuf->GetElement()), false);
            }
            UNIMPLEMENTED_MSG("Unhandled output attribute: {}", static_cast<u32>(attribute));
            return {};
        }
    }

    std::string CastOperand(const std::string& value, Type type) const {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            return value;
        case Type::Int:
            return fmt::format("ftoi({})", value);
        case Type::Uint:
            return fmt::format("ftou({})", value);
        case Type::HalfFloat:
            return fmt::format("toHalf2({})", value);
        }
        UNREACHABLE();
        return value;
    }

    std::string BitwiseCastResult(const std::string& value, Type type,
                                  bool needs_parenthesis = false) {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            if (needs_parenthesis) {
                return fmt::format("({})", value);
            }
            return value;
        case Type::Int:
            return fmt::format("itof({})", value);
        case Type::Uint:
            return fmt::format("utof({})", value);
        case Type::HalfFloat:
            return fmt::format("fromHalf2({})", value);
        }
        UNREACHABLE();
        return value;
    }

    std::string GenerateUnary(Operation operation, const std::string& func, Type result_type,
                              Type type_a, bool needs_parenthesis = true) {
        const std::string op_str = fmt::format("{}({})", func, VisitOperand(operation, 0, type_a));

        return ApplyPrecise(operation, BitwiseCastResult(op_str, result_type, needs_parenthesis));
    }

    std::string GenerateBinaryInfix(Operation operation, const std::string& func, Type result_type,
                                    Type type_a, Type type_b) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_str = fmt::format("({} {} {})", op_a, func, op_b);

        return ApplyPrecise(operation, BitwiseCastResult(op_str, result_type));
    }

    std::string GenerateBinaryCall(Operation operation, const std::string& func, Type result_type,
                                   Type type_a, Type type_b) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_str = fmt::format("{}({}, {})", func, op_a, op_b);

        return ApplyPrecise(operation, BitwiseCastResult(op_str, result_type));
    }

    std::string GenerateTernary(Operation operation, const std::string& func, Type result_type,
                                Type type_a, Type type_b, Type type_c) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_c = VisitOperand(operation, 2, type_c);
        const std::string op_str = fmt::format("{}({}, {}, {})", func, op_a, op_b, op_c);

        return ApplyPrecise(operation, BitwiseCastResult(op_str, result_type));
    }

    std::string GenerateQuaternary(Operation operation, const std::string& func, Type result_type,
                                   Type type_a, Type type_b, Type type_c, Type type_d) {
        const std::string op_a = VisitOperand(operation, 0, type_a);
        const std::string op_b = VisitOperand(operation, 1, type_b);
        const std::string op_c = VisitOperand(operation, 2, type_c);
        const std::string op_d = VisitOperand(operation, 3, type_d);
        const std::string op_str = fmt::format("{}({}, {}, {}, {})", func, op_a, op_b, op_c, op_d);

        return ApplyPrecise(operation, BitwiseCastResult(op_str, result_type));
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
            if (const auto immediate = std::get_if<ImmediateNode>(&*operand)) {
                // Inline the string as an immediate integer in GLSL (some extra arguments are
                // required to be constant)
                expr += std::to_string(static_cast<s32>(immediate->GetValue()));
            } else {
                expr += fmt::format("ftoi({})", Visit(operand));
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
            if (const auto immediate = std::get_if<ImmediateNode>(&*operand)) {
                // Inline the string as an immediate integer in GLSL (AOFFI arguments are required
                // to be constant by the standard).
                expr += std::to_string(static_cast<s32>(immediate->GetValue()));
            } else if (device.HasVariableAoffi()) {
                // Avoid using variable AOFFI on unsupported devices.
                expr += fmt::format("ftoi({})", Visit(operand));
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
        const Node& dest = operation[0];
        const Node& src = operation[1];

        std::string target;
        bool is_integer = false;

        if (const auto gpr = std::get_if<GprNode>(&*dest)) {
            if (gpr->GetIndex() == Register::ZeroIndex) {
                // Writing to Register::ZeroIndex is a no op
                return {};
            }
            target = GetRegister(gpr->GetIndex());
        } else if (const auto abuf = std::get_if<AbufNode>(&*dest)) {
            UNIMPLEMENTED_IF(abuf->IsPhysicalBuffer());
            const auto result = GetOutputAttribute(abuf);
            if (!result) {
                return {};
            }
            target = result->first;
            is_integer = result->second;
        } else if (const auto lmem = std::get_if<LmemNode>(&*dest)) {
            if (stage == ProgramType::Compute) {
                LOG_WARNING(Render_OpenGL, "Local memory is stubbed on compute shaders");
            }
            target = fmt::format("{}[ftou({}) / 4]", GetLocalMemory(), Visit(lmem->GetAddress()));
        } else if (const auto gmem = std::get_if<GmemNode>(&*dest)) {
            const std::string real = Visit(gmem->GetRealAddress());
            const std::string base = Visit(gmem->GetBaseAddress());
            const std::string final_offset = fmt::format("(ftou({}) - ftou({})) / 4", real, base);
            target = fmt::format("{}[{}]", GetGlobalMemory(gmem->GetDescriptor()), final_offset);
        } else {
            UNREACHABLE_MSG("Assign called without a proper target");
        }

        if (is_integer) {
            code.AddLine("{} = ftoi({});", target, Visit(src));
        } else {
            code.AddLine("{} = {};", target, Visit(src));
        }
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
        const std::string op_str = fmt::format("({} ? {} : {})", condition, true_case, false_case);

        return ApplyPrecise(operation, op_str);
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
        const std::string op_str = fmt::format("int({} >> {})", op_a, op_b);

        return ApplyPrecise(operation, BitwiseCastResult(op_str, Type::Int));
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
        const auto GetNegate = [&](std::size_t index) {
            return VisitOperand(operation, index, Type::Bool) + " ? -1 : 1";
        };
        const std::string value =
            fmt::format("({} * vec2({}, {}))", VisitOperand(operation, 0, Type::HalfFloat),
                        GetNegate(1), GetNegate(2));
        return BitwiseCastResult(value, Type::HalfFloat);
    }

    std::string HClamp(Operation operation) {
        const std::string value = VisitOperand(operation, 0, Type::HalfFloat);
        const std::string min = VisitOperand(operation, 1, Type::Float);
        const std::string max = VisitOperand(operation, 2, Type::Float);
        const std::string clamped = fmt::format("clamp({}, vec2({}), vec2({}))", value, min, max);

        return ApplyPrecise(operation, BitwiseCastResult(clamped, Type::HalfFloat));
    }

    std::string HUnpack(Operation operation) {
        const std::string operand{VisitOperand(operation, 0, Type::HalfFloat)};
        const auto value = [&]() -> std::string {
            switch (std::get<Tegra::Shader::HalfType>(operation.GetMeta())) {
            case Tegra::Shader::HalfType::H0_H1:
                return operand;
            case Tegra::Shader::HalfType::F32:
                return fmt::format("vec2(fromHalf2({}))", operand);
            case Tegra::Shader::HalfType::H0_H0:
                return fmt::format("vec2({}[0])", operand);
            case Tegra::Shader::HalfType::H1_H1:
                return fmt::format("vec2({}[1])", operand);
            }
            UNREACHABLE();
            return "0";
        }();
        return fmt::format("fromHalf2({})", value);
    }

    std::string HMergeF32(Operation operation) {
        return fmt::format("float(toHalf2({})[0])", Visit(operation[0]));
    }

    std::string HMergeH0(Operation operation) {
        return fmt::format("fromHalf2(vec2(toHalf2({})[0], toHalf2({})[1]))", Visit(operation[1]),
                           Visit(operation[0]));
    }

    std::string HMergeH1(Operation operation) {
        return fmt::format("fromHalf2(vec2(toHalf2({})[0], toHalf2({})[1]))", Visit(operation[0]),
                           Visit(operation[1]));
    }

    std::string HPack2(Operation operation) {
        return fmt::format("utof(packHalf2x16(vec2({}, {})))", Visit(operation[0]),
                           Visit(operation[1]));
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
        const Node& dest = operation[0];
        const Node& src = operation[1];

        std::string target;

        if (const auto pred = std::get_if<PredicateNode>(&*dest)) {
            ASSERT_MSG(!pred->IsNegated(), "Negating logical assignment");

            const auto index = pred->GetIndex();
            switch (index) {
            case Tegra::Shader::Pred::NeverExecute:
            case Tegra::Shader::Pred::UnusedIndex:
                // Writing to these predicates is a no-op
                return {};
            }
            target = GetPredicate(index);
        } else if (const auto flag = std::get_if<InternalFlagNode>(&*dest)) {
            target = GetInternalFlag(flag->GetFlag());
        }

        code.AddLine("{} = {};", target, Visit(src));
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
        return fmt::format("{}[{}]", pair, VisitOperand(operation, 1, Type::Uint));
    }

    std::string LogicalAnd2(Operation operation) {
        return GenerateUnary(operation, "all", Type::Bool, Type::Bool2);
    }

    template <bool with_nan>
    std::string GenerateHalfComparison(Operation operation, const std::string& compare_op) {
        const std::string comparison{GenerateBinaryCall(operation, compare_op, Type::Bool2,
                                                        Type::HalfFloat, Type::HalfFloat)};
        if constexpr (!with_nan) {
            return comparison;
        }
        return fmt::format("halfFloatNanComparison({}, {}, {})", comparison,
                           VisitOperand(operation, 0, Type::HalfFloat),
                           VisitOperand(operation, 1, Type::HalfFloat));
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
            return fmt::format("itof(int(textureSize({}, {}){}))", sampler, lod,
                               GetSwizzle(meta->element));
        case 2:
            return "0";
        case 3:
            return fmt::format("itof(textureQueryLevels({}))", sampler);
        }
        UNREACHABLE();
        return "0";
    }

    std::string TextureQueryLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);

        if (meta->element < 2) {
            return fmt::format("itof(int(({} * vec2(256)){}))",
                               GenerateTexture(operation, "QueryLod", {}),
                               GetSwizzle(meta->element));
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

        // Store a copy of the expression without the lod to be used with texture buffers
        std::string expr_buffer = expr;

        if (meta->lod) {
            expr += ", ";
            expr += CastOperand(Visit(meta->lod), Type::Int);
        }
        expr += ')';
        expr += GetSwizzle(meta->element);

        expr_buffer += ')';
        expr_buffer += GetSwizzle(meta->element);

        const std::string tmp{code.GenerateTemporary()};
        EmitIfdefIsBuffer(meta->sampler);
        code.AddLine("float {} = {};", tmp, expr_buffer);
        code.AddLine("#else");
        code.AddLine("float {} = {};", tmp, expr);
        code.AddLine("#endif");

        return tmp;
    }

    std::string ImageStore(Operation operation) {
        constexpr std::array<const char*, 4> constructors{"int(", "ivec2(", "ivec3(", "ivec4("};
        const auto meta{std::get<MetaImage>(operation.GetMeta())};

        std::string expr = "imageStore(";
        expr += GetImage(meta.image);
        expr += ", ";

        const std::size_t coords_count{operation.GetOperandsCount()};
        expr += constructors.at(coords_count - 1);
        for (std::size_t i = 0; i < coords_count; ++i) {
            expr += VisitOperand(operation, i, Type::Int);
            if (i + 1 < coords_count) {
                expr += ", ";
            }
        }
        expr += "), ";

        const std::size_t values_count{meta.values.size()};
        UNIMPLEMENTED_IF(values_count != 4);
        expr += "vec4(";
        for (std::size_t i = 0; i < values_count; ++i) {
            expr += Visit(meta.values.at(i));
            if (i + 1 < values_count) {
                expr += ", ";
            }
        }
        expr += "));";

        code.AddLine(expr);
        return {};
    }

    std::string Branch(Operation operation) {
        const auto target = std::get_if<ImmediateNode>(&*operation[0]);
        UNIMPLEMENTED_IF(!target);

        code.AddLine("jmp_to = 0x{:x}u;", target->GetValue());
        code.AddLine("break;");
        return {};
    }

    std::string BranchIndirect(Operation operation) {
        const std::string op_a = VisitOperand(operation, 0, Type::Uint);

        code.AddLine("jmp_to = {};", op_a);
        code.AddLine("break;");
        return {};
    }

    std::string PushFlowStack(Operation operation) {
        const auto stack = std::get<MetaStackClass>(operation.GetMeta());
        const auto target = std::get_if<ImmediateNode>(&*operation[0]);
        UNIMPLEMENTED_IF(!target);

        code.AddLine("{}[{}++] = 0x{:x}u;", FlowStackName(stack), FlowStackTopName(stack),
                     target->GetValue());
        return {};
    }

    std::string PopFlowStack(Operation operation) {
        const auto stack = std::get<MetaStackClass>(operation.GetMeta());
        code.AddLine("jmp_to = {}[--{}];", FlowStackName(stack), FlowStackTopName(stack));
        code.AddLine("break;");
        return {};
    }

    std::string Exit(Operation operation) {
        if (stage != ProgramType::Fragment) {
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

        // Write the color outputs using the data in the shader registers, disabled
        // rendertargets/components are skipped in the register assignment.
        u32 current_reg = 0;
        for (u32 render_target = 0; render_target < Maxwell::NumRenderTargets; ++render_target) {
            // TODO(Subv): Figure out how dual-source blending is configured in the Switch.
            for (u32 component = 0; component < 4; ++component) {
                if (header.ps.IsColorComponentOutputEnabled(render_target, component)) {
                    code.AddLine("FragColor{}[{}] = {};", render_target, component,
                                 SafeGetRegister(current_reg));
                    ++current_reg;
                }
            }
        }

        if (header.ps.omap.depth) {
            // The depth output is always 2 registers after the last color output, and current_reg
            // already contains one past the last color register.
            code.AddLine("gl_FragDepth = {};", SafeGetRegister(current_reg + 1));
        }

        code.AddLine("return;");
        return {};
    }

    std::string Discard(Operation operation) {
        // Enclose "discard" in a conditional, so that GLSL compilation does not complain
        // about unexecuted instructions that may follow this.
        code.AddLine("if (true) {{");
        ++code.scope;
        code.AddLine("discard;");
        --code.scope;
        code.AddLine("}}");
        return {};
    }

    std::string EmitVertex(Operation operation) {
        ASSERT_MSG(stage == ProgramType::Geometry,
                   "EmitVertex is expected to be used in a geometry shader.");

        // If a geometry shader is attached, it will always flip (it's the last stage before
        // fragment). For more info about flipping, refer to gl_shader_gen.cpp.
        code.AddLine("gl_Position.xy *= viewport_flip.xy;");
        code.AddLine("EmitVertex();");
        return {};
    }

    std::string EndPrimitive(Operation operation) {
        ASSERT_MSG(stage == ProgramType::Geometry,
                   "EndPrimitive is expected to be used in a geometry shader.");

        code.AddLine("EndPrimitive();");
        return {};
    }

    std::string YNegate(Operation operation) {
        // Config pack's third value is Y_NEGATE's state.
        return "uintBitsToFloat(config_pack[2])";
    }

    template <u32 element>
    std::string LocalInvocationId(Operation) {
        return "utof(gl_LocalInvocationID"s + GetSwizzle(element) + ')';
    }

    template <u32 element>
    std::string WorkGroupId(Operation) {
        return "utof(gl_WorkGroupID"s + GetSwizzle(element) + ')';
    }

    static constexpr std::array operation_decompilers = {
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
        &GLSLDecompiler::LogicalAnd2,

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

        &GLSLDecompiler::ImageStore,

        &GLSLDecompiler::Branch,
        &GLSLDecompiler::BranchIndirect,
        &GLSLDecompiler::PushFlowStack,
        &GLSLDecompiler::PopFlowStack,
        &GLSLDecompiler::Exit,
        &GLSLDecompiler::Discard,

        &GLSLDecompiler::EmitVertex,
        &GLSLDecompiler::EndPrimitive,

        &GLSLDecompiler::YNegate,
        &GLSLDecompiler::LocalInvocationId<0>,
        &GLSLDecompiler::LocalInvocationId<1>,
        &GLSLDecompiler::LocalInvocationId<2>,
        &GLSLDecompiler::WorkGroupId<0>,
        &GLSLDecompiler::WorkGroupId<1>,
        &GLSLDecompiler::WorkGroupId<2>,
    };
    static_assert(operation_decompilers.size() == static_cast<std::size_t>(OperationCode::Amount));

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

        return fmt::format("{}_{}", InternalFlagNames[index], suffix);
    }

    std::string GetSampler(const Sampler& sampler) const {
        return GetDeclarationWithSuffix(static_cast<u32>(sampler.GetIndex()), "sampler");
    }

    std::string GetImage(const Image& image) const {
        return GetDeclarationWithSuffix(static_cast<u32>(image.GetIndex()), "image");
    }

    void EmitIfdefIsBuffer(const Sampler& sampler) {
        code.AddLine("#ifdef SAMPLER_{}_IS_BUFFER", sampler.GetIndex());
    }

    std::string GetDeclarationWithSuffix(u32 index, const std::string& name) const {
        return fmt::format("{}_{}_{}", name, index, suffix);
    }

    u32 GetNumPhysicalInputAttributes() const {
        return IsVertexShader(stage) ? GetNumPhysicalAttributes() : GetNumPhysicalVaryings();
    }

    u32 GetNumPhysicalAttributes() const {
        return std::min<u32>(device.GetMaxVertexAttributes(), Maxwell::NumVertexAttributes);
    }

    u32 GetNumPhysicalVaryings() const {
        return std::min<u32>(device.GetMaxVaryings(), Maxwell::NumVaryings);
    }

    const Device& device;
    const ShaderIR& ir;
    const ProgramType stage;
    const std::string suffix;
    const Header header;

    ShaderWriter code;
};

} // Anonymous namespace

std::string GetCommonDeclarations() {
    return fmt::format(
        "#define MAX_CONSTBUFFER_ELEMENTS {}\n"
        "#define ftoi floatBitsToInt\n"
        "#define ftou floatBitsToUint\n"
        "#define itof intBitsToFloat\n"
        "#define utof uintBitsToFloat\n\n"
        "float fromHalf2(vec2 pair) {{\n"
        "    return utof(packHalf2x16(pair));\n"
        "}}\n\n"
        "vec2 toHalf2(float value) {{\n"
        "    return unpackHalf2x16(ftou(value));\n"
        "}}\n\n"
        "bvec2 halfFloatNanComparison(bvec2 comparison, vec2 pair1, vec2 pair2) {{\n"
        "    bvec2 is_nan1 = isnan(pair1);\n"
        "    bvec2 is_nan2 = isnan(pair2);\n"
        "    return bvec2(comparison.x || is_nan1.x || is_nan2.x, comparison.y || is_nan1.y || "
        "is_nan2.y);\n"
        "}}\n",
        MAX_CONSTBUFFER_ELEMENTS);
}

ProgramResult Decompile(const Device& device, const ShaderIR& ir, ProgramType stage,
                        const std::string& suffix) {
    GLSLDecompiler decompiler(device, ir, stage, suffix);
    decompiler.Decompile();
    return {decompiler.GetResult(), decompiler.GetShaderEntries()};
}

} // namespace OpenGL::GLShader
