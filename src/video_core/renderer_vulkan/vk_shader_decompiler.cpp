// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <functional>
#include <map>
#include <set>

#include <fmt/format.h>

#include <sirit/sirit.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/shader_bytecode.h"
#include "video_core/engines/shader_header.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_shader_decompiler.h"
#include "video_core/shader/node.h"
#include "video_core/shader/shader_ir.h"

namespace Vulkan::VKShader {

using Sirit::Id;
using Tegra::Shader::Attribute;
using Tegra::Shader::AttributeUse;
using Tegra::Shader::Register;
using namespace VideoCommon::Shader;

using Maxwell = Tegra::Engines::Maxwell3D::Regs;
using ShaderStage = Tegra::Engines::Maxwell3D::Regs::ShaderStage;
using Operation = const OperationNode&;

// TODO(Rodrigo): Use rasterizer's value
constexpr u32 MAX_CONSTBUFFER_FLOATS = 0x4000;
constexpr u32 MAX_CONSTBUFFER_ELEMENTS = MAX_CONSTBUFFER_FLOATS / 4;
constexpr u32 STAGE_BINDING_STRIDE = 0x100;

enum class Type { Bool, Bool2, Float, Int, Uint, HalfFloat };

struct SamplerImage {
    Id image_type;
    Id sampled_image_type;
    Id sampler;
};

namespace {

spv::Dim GetSamplerDim(const Sampler& sampler) {
    switch (sampler.GetType()) {
    case Tegra::Shader::TextureType::Texture1D:
        return spv::Dim::Dim1D;
    case Tegra::Shader::TextureType::Texture2D:
        return spv::Dim::Dim2D;
    case Tegra::Shader::TextureType::Texture3D:
        return spv::Dim::Dim3D;
    case Tegra::Shader::TextureType::TextureCube:
        return spv::Dim::Cube;
    default:
        UNIMPLEMENTED_MSG("Unimplemented sampler type={}", static_cast<u32>(sampler.GetType()));
        return spv::Dim::Dim2D;
    }
}

/// Returns true if an attribute index is one of the 32 generic attributes
constexpr bool IsGenericAttribute(Attribute::Index attribute) {
    return attribute >= Attribute::Index::Attribute_0 &&
           attribute <= Attribute::Index::Attribute_31;
}

/// Returns the location of a generic attribute
constexpr u32 GetGenericAttributeLocation(Attribute::Index attribute) {
    ASSERT(IsGenericAttribute(attribute));
    return static_cast<u32>(attribute) - static_cast<u32>(Attribute::Index::Attribute_0);
}

/// Returns true if an object has to be treated as precise
bool IsPrecise(Operation operand) {
    const auto& meta{operand.GetMeta()};
    if (std::holds_alternative<MetaArithmetic>(meta)) {
        return std::get<MetaArithmetic>(meta).precise;
    }
    return false;
}

} // namespace

class SPIRVDecompiler : public Sirit::Module {
public:
    explicit SPIRVDecompiler(const VKDevice& device, const ShaderIR& ir, ShaderStage stage)
        : Module(0x00010300), device{device}, ir{ir}, stage{stage}, header{ir.GetHeader()} {
        AddCapability(spv::Capability::Shader);
        AddExtension("SPV_KHR_storage_buffer_storage_class");
        AddExtension("SPV_KHR_variable_pointers");
    }

    void Decompile() {
        AllocateBindings();
        AllocateLabels();

        DeclareVertex();
        DeclareGeometry();
        DeclareFragment();
        DeclareRegisters();
        DeclarePredicates();
        DeclareLocalMemory();
        DeclareInternalFlags();
        DeclareInputAttributes();
        DeclareOutputAttributes();
        DeclareConstantBuffers();
        DeclareGlobalBuffers();
        DeclareSamplers();

        execute_function =
            Emit(OpFunction(t_void, spv::FunctionControlMask::Inline, TypeFunction(t_void)));
        Emit(OpLabel());

        const u32 first_address = ir.GetBasicBlocks().begin()->first;
        const Id loop_label = OpLabel("loop");
        const Id merge_label = OpLabel("merge");
        const Id dummy_label = OpLabel();
        const Id jump_label = OpLabel();
        continue_label = OpLabel("continue");

        std::vector<Sirit::Literal> literals;
        std::vector<Id> branch_labels;
        for (const auto& pair : labels) {
            const auto [literal, label] = pair;
            literals.push_back(literal);
            branch_labels.push_back(label);
        }

        jmp_to = Emit(OpVariable(TypePointer(spv::StorageClass::Function, t_uint),
                                 spv::StorageClass::Function, Constant(t_uint, first_address)));
        std::tie(ssy_flow_stack, ssy_flow_stack_top) = CreateFlowStack();
        std::tie(pbk_flow_stack, pbk_flow_stack_top) = CreateFlowStack();

        Name(jmp_to, "jmp_to");
        Name(ssy_flow_stack, "ssy_flow_stack");
        Name(ssy_flow_stack_top, "ssy_flow_stack_top");
        Name(pbk_flow_stack, "pbk_flow_stack");
        Name(pbk_flow_stack_top, "pbk_flow_stack_top");

        Emit(OpBranch(loop_label));
        Emit(loop_label);
        Emit(OpLoopMerge(merge_label, continue_label, spv::LoopControlMask::Unroll));
        Emit(OpBranch(dummy_label));

        Emit(dummy_label);
        const Id default_branch = OpLabel();
        const Id jmp_to_load = Emit(OpLoad(t_uint, jmp_to));
        Emit(OpSelectionMerge(jump_label, spv::SelectionControlMask::MaskNone));
        Emit(OpSwitch(jmp_to_load, default_branch, literals, branch_labels));

        Emit(default_branch);
        Emit(OpReturn());

        for (const auto& pair : ir.GetBasicBlocks()) {
            const auto& [address, bb] = pair;
            Emit(labels.at(address));

            VisitBasicBlock(bb);

            const auto next_it = labels.lower_bound(address + 1);
            const Id next_label = next_it != labels.end() ? next_it->second : default_branch;
            Emit(OpBranch(next_label));
        }

        Emit(jump_label);
        Emit(OpBranch(continue_label));
        Emit(continue_label);
        Emit(OpBranch(loop_label));
        Emit(merge_label);
        Emit(OpReturn());
        Emit(OpFunctionEnd());
    }

    ShaderEntries GetShaderEntries() const {
        ShaderEntries entries;
        entries.const_buffers_base_binding = const_buffers_base_binding;
        entries.global_buffers_base_binding = global_buffers_base_binding;
        entries.samplers_base_binding = samplers_base_binding;
        for (const auto& cbuf : ir.GetConstantBuffers()) {
            entries.const_buffers.emplace_back(cbuf.second, cbuf.first);
        }
        for (const auto& gmem_pair : ir.GetGlobalMemory()) {
            const auto& [base, usage] = gmem_pair;
            entries.global_buffers.emplace_back(base.cbuf_index, base.cbuf_offset);
        }
        for (const auto& [offset, sampler] : ir.GetSamplers()) {
            entries.samplers.emplace_back(sampler);
        }
        for (const auto& attribute : ir.GetInputAttributes()) {
            if (IsGenericAttribute(attribute)) {
                entries.attributes.insert(GetGenericAttributeLocation(attribute));
            }
        }
        entries.clip_distances = ir.GetClipDistances();
        entries.shader_length = ir.GetLength();
        entries.entry_function = execute_function;
        entries.interfaces = interfaces;
        return entries;
    }

private:
    static constexpr auto INTERNAL_FLAGS_COUNT = static_cast<std::size_t>(InternalFlag::Amount);

    void AllocateBindings() {
        const u32 binding_base = static_cast<u32>(stage) * STAGE_BINDING_STRIDE;
        u32 binding_iterator = binding_base;

        const auto Allocate = [&binding_iterator](std::size_t count) {
            const u32 current_binding = binding_iterator;
            binding_iterator += static_cast<u32>(count);
            return current_binding;
        };
        const_buffers_base_binding = Allocate(ir.GetConstantBuffers().size());
        global_buffers_base_binding = Allocate(ir.GetGlobalMemory().size());
        samplers_base_binding = Allocate(ir.GetSamplers().size());

        ASSERT_MSG(binding_iterator - binding_base < STAGE_BINDING_STRIDE,
                   "Stage binding stride is too small");
    }

    void AllocateLabels() {
        for (const auto& pair : ir.GetBasicBlocks()) {
            const u32 address = pair.first;
            labels.emplace(address, OpLabel(fmt::format("label_0x{:x}", address)));
        }
    }

    void DeclareVertex() {
        if (stage != ShaderStage::Vertex)
            return;

        DeclareVertexRedeclarations();
    }

    void DeclareGeometry() {
        if (stage != ShaderStage::Geometry)
            return;

        UNIMPLEMENTED();
    }

    void DeclareFragment() {
        if (stage != ShaderStage::Fragment)
            return;

        for (u32 rt = 0; rt < static_cast<u32>(frag_colors.size()); ++rt) {
            if (!IsRenderTargetUsed(rt)) {
                continue;
            }

            const Id id = AddGlobalVariable(OpVariable(t_out_float4, spv::StorageClass::Output));
            Name(id, fmt::format("frag_color{}", rt));
            Decorate(id, spv::Decoration::Location, rt);

            frag_colors[rt] = id;
            interfaces.push_back(id);
        }

        if (header.ps.omap.depth) {
            frag_depth = AddGlobalVariable(OpVariable(t_out_float, spv::StorageClass::Output));
            Name(frag_depth, "frag_depth");
            Decorate(frag_depth, spv::Decoration::BuiltIn,
                     static_cast<u32>(spv::BuiltIn::FragDepth));

            interfaces.push_back(frag_depth);
        }

        frag_coord = DeclareBuiltIn(spv::BuiltIn::FragCoord, spv::StorageClass::Input, t_in_float4,
                                    "frag_coord");
        front_facing = DeclareBuiltIn(spv::BuiltIn::FrontFacing, spv::StorageClass::Input,
                                      t_in_bool, "front_facing");
    }

    void DeclareRegisters() {
        for (const u32 gpr : ir.GetRegisters()) {
            const Id id = OpVariable(t_prv_float, spv::StorageClass::Private, v_float_zero);
            Name(id, fmt::format("gpr_{}", gpr));
            registers.emplace(gpr, AddGlobalVariable(id));
        }
    }

    void DeclarePredicates() {
        for (const auto pred : ir.GetPredicates()) {
            const Id id = OpVariable(t_prv_bool, spv::StorageClass::Private, v_false);
            Name(id, fmt::format("pred_{}", static_cast<u32>(pred)));
            predicates.emplace(pred, AddGlobalVariable(id));
        }
    }

    void DeclareLocalMemory() {
        if (const u64 local_memory_size = header.GetLocalMemorySize(); local_memory_size > 0) {
            const auto element_count = static_cast<u32>(Common::AlignUp(local_memory_size, 4) / 4);
            const Id type_array = TypeArray(t_float, Constant(t_uint, element_count));
            const Id type_pointer = TypePointer(spv::StorageClass::Private, type_array);
            Name(type_pointer, "LocalMemory");

            local_memory =
                OpVariable(type_pointer, spv::StorageClass::Private, ConstantNull(type_array));
            AddGlobalVariable(Name(local_memory, "local_memory"));
        }
    }

    void DeclareInternalFlags() {
        constexpr std::array<const char*, INTERNAL_FLAGS_COUNT> names = {"zero", "sign", "carry",
                                                                         "overflow"};
        for (std::size_t flag = 0; flag < INTERNAL_FLAGS_COUNT; ++flag) {
            const auto flag_code = static_cast<InternalFlag>(flag);
            const Id id = OpVariable(t_prv_bool, spv::StorageClass::Private, v_false);
            internal_flags[flag] = AddGlobalVariable(Name(id, names[flag]));
        }
    }

    void DeclareInputAttributes() {
        for (const auto index : ir.GetInputAttributes()) {
            if (!IsGenericAttribute(index)) {
                continue;
            }

            UNIMPLEMENTED_IF(stage == ShaderStage::Geometry);

            const u32 location = GetGenericAttributeLocation(index);
            const Id id = OpVariable(t_in_float4, spv::StorageClass::Input);
            Name(AddGlobalVariable(id), fmt::format("in_attr{}", location));
            input_attributes.emplace(index, id);
            interfaces.push_back(id);

            Decorate(id, spv::Decoration::Location, location);

            if (stage != ShaderStage::Fragment) {
                continue;
            }
            switch (header.ps.GetAttributeUse(location)) {
            case AttributeUse::Constant:
                Decorate(id, spv::Decoration::Flat);
                break;
            case AttributeUse::ScreenLinear:
                Decorate(id, spv::Decoration::NoPerspective);
                break;
            case AttributeUse::Perspective:
                // Default
                break;
            default:
                UNREACHABLE_MSG("Unused attribute being fetched");
            }
        }
    }

    void DeclareOutputAttributes() {
        for (const auto index : ir.GetOutputAttributes()) {
            if (!IsGenericAttribute(index)) {
                continue;
            }
            const auto location = GetGenericAttributeLocation(index);
            const Id id = OpVariable(t_out_float4, spv::StorageClass::Output);
            Name(AddGlobalVariable(id), fmt::format("out_attr{}", location));
            output_attributes.emplace(index, id);
            interfaces.push_back(id);

            Decorate(id, spv::Decoration::Location, location);
        }
    }

    void DeclareConstantBuffers() {
        u32 binding = const_buffers_base_binding;
        for (const auto& entry : ir.GetConstantBuffers()) {
            const auto [index, size] = entry;
            const Id type = device.IsKhrUniformBufferStandardLayoutSupported() ? t_cbuf_scalar_ubo
                                                                               : t_cbuf_std140_ubo;
            const Id id = OpVariable(type, spv::StorageClass::Uniform);
            AddGlobalVariable(Name(id, fmt::format("cbuf_{}", index)));

            Decorate(id, spv::Decoration::Binding, binding++);
            Decorate(id, spv::Decoration::DescriptorSet, DESCRIPTOR_SET);
            constant_buffers.emplace(index, id);
        }
    }

    void DeclareGlobalBuffers() {
        u32 binding = global_buffers_base_binding;
        for (const auto& entry : ir.GetGlobalMemory()) {
            const auto [base, usage] = entry;
            const Id id = OpVariable(t_gmem_ssbo, spv::StorageClass::StorageBuffer);
            AddGlobalVariable(
                Name(id, fmt::format("gmem_{}_{}", base.cbuf_index, base.cbuf_offset)));

            Decorate(id, spv::Decoration::Binding, binding++);
            Decorate(id, spv::Decoration::DescriptorSet, DESCRIPTOR_SET);
            global_buffers.emplace(base, id);
        }
    }

    void DeclareSamplers() {
        u32 binding = samplers_base_binding;
        for (const auto& [offset, sampler] : ir.GetSamplers()) {
            const auto dim = GetSamplerDim(sampler);
            const int depth = sampler.IsShadow() ? 1 : 0;
            const int arrayed = sampler.IsArray() ? 1 : 0;
            // TODO(Rodrigo): Sampled 1 indicates that the image will be used with a sampler. When
            // SULD and SUST instructions are implemented, replace this value.
            const int sampled = 1;
            const Id image_type =
                TypeImage(t_float, dim, depth, arrayed, false, sampled, spv::ImageFormat::Unknown);
            const Id sampled_image_type = TypeSampledImage(image_type);
            const Id pointer_type =
                TypePointer(spv::StorageClass::UniformConstant, sampled_image_type);
            const Id id = OpVariable(pointer_type, spv::StorageClass::UniformConstant);
            AddGlobalVariable(Name(id, fmt::format("sampler_{}", sampler.GetIndex())));

            sampler_images.insert(
                {static_cast<u32>(sampler.GetIndex()), {image_type, sampled_image_type, id}});

            Decorate(id, spv::Decoration::Binding, binding++);
            Decorate(id, spv::Decoration::DescriptorSet, DESCRIPTOR_SET);
        }
    }

    void DeclareVertexRedeclarations() {
        vertex_index = DeclareBuiltIn(spv::BuiltIn::VertexIndex, spv::StorageClass::Input,
                                      t_in_uint, "vertex_index");
        instance_index = DeclareBuiltIn(spv::BuiltIn::InstanceIndex, spv::StorageClass::Input,
                                        t_in_uint, "instance_index");

        bool is_clip_distances_declared = false;
        for (const auto index : ir.GetOutputAttributes()) {
            if (index == Attribute::Index::ClipDistances0123 ||
                index == Attribute::Index::ClipDistances4567) {
                is_clip_distances_declared = true;
            }
        }

        std::vector<Id> members;
        members.push_back(t_float4);
        if (ir.UsesPointSize()) {
            members.push_back(t_float);
        }
        if (is_clip_distances_declared) {
            members.push_back(TypeArray(t_float, Constant(t_uint, 8)));
        }

        const Id gl_per_vertex_struct = Name(TypeStruct(members), "PerVertex");
        Decorate(gl_per_vertex_struct, spv::Decoration::Block);

        u32 declaration_index = 0;
        const auto MemberDecorateBuiltIn = [&](spv::BuiltIn builtin, std::string name,
                                               bool condition) {
            if (!condition)
                return u32{};
            MemberName(gl_per_vertex_struct, declaration_index, name);
            MemberDecorate(gl_per_vertex_struct, declaration_index, spv::Decoration::BuiltIn,
                           static_cast<u32>(builtin));
            return declaration_index++;
        };

        position_index = MemberDecorateBuiltIn(spv::BuiltIn::Position, "position", true);
        point_size_index =
            MemberDecorateBuiltIn(spv::BuiltIn::PointSize, "point_size", ir.UsesPointSize());
        clip_distances_index = MemberDecorateBuiltIn(spv::BuiltIn::ClipDistance, "clip_distances",
                                                     is_clip_distances_declared);

        const Id type_pointer = TypePointer(spv::StorageClass::Output, gl_per_vertex_struct);
        per_vertex = OpVariable(type_pointer, spv::StorageClass::Output);
        AddGlobalVariable(Name(per_vertex, "per_vertex"));
        interfaces.push_back(per_vertex);
    }

    void VisitBasicBlock(const NodeBlock& bb) {
        for (const auto& node : bb) {
            static_cast<void>(Visit(node));
        }
    }

    Id Visit(const Node& node) {
        if (const auto operation = std::get_if<OperationNode>(&*node)) {
            const auto operation_index = static_cast<std::size_t>(operation->GetCode());
            const auto decompiler = operation_decompilers[operation_index];
            if (decompiler == nullptr) {
                UNREACHABLE_MSG("Operation decompiler {} not defined", operation_index);
            }
            return (this->*decompiler)(*operation);

        } else if (const auto gpr = std::get_if<GprNode>(&*node)) {
            const u32 index = gpr->GetIndex();
            if (index == Register::ZeroIndex) {
                return Constant(t_float, 0.0f);
            }
            return Emit(OpLoad(t_float, registers.at(index)));

        } else if (const auto immediate = std::get_if<ImmediateNode>(&*node)) {
            return BitcastTo<Type::Float>(Constant(t_uint, immediate->GetValue()));

        } else if (const auto predicate = std::get_if<PredicateNode>(&*node)) {
            const auto value = [&]() -> Id {
                switch (const auto index = predicate->GetIndex(); index) {
                case Tegra::Shader::Pred::UnusedIndex:
                    return v_true;
                case Tegra::Shader::Pred::NeverExecute:
                    return v_false;
                default:
                    return Emit(OpLoad(t_bool, predicates.at(index)));
                }
            }();
            if (predicate->IsNegated()) {
                return Emit(OpLogicalNot(t_bool, value));
            }
            return value;

        } else if (const auto abuf = std::get_if<AbufNode>(&*node)) {
            const auto attribute = abuf->GetIndex();
            const auto element = abuf->GetElement();

            switch (attribute) {
            case Attribute::Index::Position:
                if (stage != ShaderStage::Fragment) {
                    UNIMPLEMENTED();
                    break;
                } else {
                    if (element == 3) {
                        return Constant(t_float, 1.0f);
                    }
                    return Emit(OpLoad(t_float, AccessElement(t_in_float, frag_coord, element)));
                }
            case Attribute::Index::TessCoordInstanceIDVertexID:
                // TODO(Subv): Find out what the values are for the first two elements when inside a
                // vertex shader, and what's the value of the fourth element when inside a Tess Eval
                // shader.
                ASSERT(stage == ShaderStage::Vertex);
                switch (element) {
                case 2:
                    return BitcastFrom<Type::Uint>(Emit(OpLoad(t_uint, instance_index)));
                case 3:
                    return BitcastFrom<Type::Uint>(Emit(OpLoad(t_uint, vertex_index)));
                }
                UNIMPLEMENTED_MSG("Unmanaged TessCoordInstanceIDVertexID element={}", element);
                return Constant(t_float, 0);
            case Attribute::Index::FrontFacing:
                // TODO(Subv): Find out what the values are for the other elements.
                ASSERT(stage == ShaderStage::Fragment);
                if (element == 3) {
                    const Id is_front_facing = Emit(OpLoad(t_bool, front_facing));
                    const Id true_value =
                        BitcastTo<Type::Float>(Constant(t_int, static_cast<s32>(-1)));
                    const Id false_value = BitcastTo<Type::Float>(Constant(t_int, 0));
                    return Emit(OpSelect(t_float, is_front_facing, true_value, false_value));
                }
                UNIMPLEMENTED_MSG("Unmanaged FrontFacing element={}", element);
                return Constant(t_float, 0.0f);
            default:
                if (IsGenericAttribute(attribute)) {
                    const Id pointer =
                        AccessElement(t_in_float, input_attributes.at(attribute), element);
                    return Emit(OpLoad(t_float, pointer));
                }
                break;
            }
            UNIMPLEMENTED_MSG("Unhandled input attribute: {}", static_cast<u32>(attribute));

        } else if (const auto cbuf = std::get_if<CbufNode>(&*node)) {
            const Node& offset = cbuf->GetOffset();
            const Id buffer_id = constant_buffers.at(cbuf->GetIndex());

            Id pointer{};
            if (device.IsKhrUniformBufferStandardLayoutSupported()) {
                const Id buffer_offset = Emit(OpShiftRightLogical(
                    t_uint, BitcastTo<Type::Uint>(Visit(offset)), Constant(t_uint, 2u)));
                pointer = Emit(
                    OpAccessChain(t_cbuf_float, buffer_id, Constant(t_uint, 0u), buffer_offset));
            } else {
                Id buffer_index{};
                Id buffer_element{};
                if (const auto immediate = std::get_if<ImmediateNode>(&*offset)) {
                    // Direct access
                    const u32 offset_imm = immediate->GetValue();
                    ASSERT(offset_imm % 4 == 0);
                    buffer_index = Constant(t_uint, offset_imm / 16);
                    buffer_element = Constant(t_uint, (offset_imm / 4) % 4);
                } else if (std::holds_alternative<OperationNode>(*offset)) {
                    // Indirect access
                    const Id offset_id = BitcastTo<Type::Uint>(Visit(offset));
                    const Id unsafe_offset = Emit(OpUDiv(t_uint, offset_id, Constant(t_uint, 4)));
                    const Id final_offset = Emit(OpUMod(
                        t_uint, unsafe_offset, Constant(t_uint, MAX_CONSTBUFFER_ELEMENTS - 1)));
                    buffer_index = Emit(OpUDiv(t_uint, final_offset, Constant(t_uint, 4)));
                    buffer_element = Emit(OpUMod(t_uint, final_offset, Constant(t_uint, 4)));
                } else {
                    UNREACHABLE_MSG("Unmanaged offset node type");
                }
                pointer = Emit(OpAccessChain(t_cbuf_float, buffer_id, Constant(t_uint, 0),
                                             buffer_index, buffer_element));
            }
            return Emit(OpLoad(t_float, pointer));

        } else if (const auto gmem = std::get_if<GmemNode>(&*node)) {
            const Id gmem_buffer = global_buffers.at(gmem->GetDescriptor());
            const Id real = BitcastTo<Type::Uint>(Visit(gmem->GetRealAddress()));
            const Id base = BitcastTo<Type::Uint>(Visit(gmem->GetBaseAddress()));

            Id offset = Emit(OpISub(t_uint, real, base));
            offset = Emit(OpUDiv(t_uint, offset, Constant(t_uint, 4u)));
            return Emit(OpLoad(t_float, Emit(OpAccessChain(t_gmem_float, gmem_buffer,
                                                           Constant(t_uint, 0u), offset))));

        } else if (const auto conditional = std::get_if<ConditionalNode>(&*node)) {
            // It's invalid to call conditional on nested nodes, use an operation instead
            const Id true_label = OpLabel();
            const Id skip_label = OpLabel();
            const Id condition = Visit(conditional->GetCondition());
            Emit(OpSelectionMerge(skip_label, spv::SelectionControlMask::MaskNone));
            Emit(OpBranchConditional(condition, true_label, skip_label));
            Emit(true_label);

            VisitBasicBlock(conditional->GetCode());

            Emit(OpBranch(skip_label));
            Emit(skip_label);
            return {};

        } else if (const auto comment = std::get_if<CommentNode>(&*node)) {
            Name(Emit(OpUndef(t_void)), comment->GetText());
            return {};
        }

        UNREACHABLE();
        return {};
    }

    template <Id (Module::*func)(Id, Id), Type result_type, Type type_a = result_type>
    Id Unary(Operation operation) {
        const Id type_def = GetTypeDefinition(result_type);
        const Id op_a = VisitOperand<type_a>(operation, 0);

        const Id value = BitcastFrom<result_type>(Emit((this->*func)(type_def, op_a)));
        if (IsPrecise(operation)) {
            Decorate(value, spv::Decoration::NoContraction);
        }
        return value;
    }

    template <Id (Module::*func)(Id, Id, Id), Type result_type, Type type_a = result_type,
              Type type_b = type_a>
    Id Binary(Operation operation) {
        const Id type_def = GetTypeDefinition(result_type);
        const Id op_a = VisitOperand<type_a>(operation, 0);
        const Id op_b = VisitOperand<type_b>(operation, 1);

        const Id value = BitcastFrom<result_type>(Emit((this->*func)(type_def, op_a, op_b)));
        if (IsPrecise(operation)) {
            Decorate(value, spv::Decoration::NoContraction);
        }
        return value;
    }

    template <Id (Module::*func)(Id, Id, Id, Id), Type result_type, Type type_a = result_type,
              Type type_b = type_a, Type type_c = type_b>
    Id Ternary(Operation operation) {
        const Id type_def = GetTypeDefinition(result_type);
        const Id op_a = VisitOperand<type_a>(operation, 0);
        const Id op_b = VisitOperand<type_b>(operation, 1);
        const Id op_c = VisitOperand<type_c>(operation, 2);

        const Id value = BitcastFrom<result_type>(Emit((this->*func)(type_def, op_a, op_b, op_c)));
        if (IsPrecise(operation)) {
            Decorate(value, spv::Decoration::NoContraction);
        }
        return value;
    }

    template <Id (Module::*func)(Id, Id, Id, Id, Id), Type result_type, Type type_a = result_type,
              Type type_b = type_a, Type type_c = type_b, Type type_d = type_c>
    Id Quaternary(Operation operation) {
        const Id type_def = GetTypeDefinition(result_type);
        const Id op_a = VisitOperand<type_a>(operation, 0);
        const Id op_b = VisitOperand<type_b>(operation, 1);
        const Id op_c = VisitOperand<type_c>(operation, 2);
        const Id op_d = VisitOperand<type_d>(operation, 3);

        const Id value =
            BitcastFrom<result_type>(Emit((this->*func)(type_def, op_a, op_b, op_c, op_d)));
        if (IsPrecise(operation)) {
            Decorate(value, spv::Decoration::NoContraction);
        }
        return value;
    }

    Id Assign(Operation operation) {
        const Node& dest = operation[0];
        const Node& src = operation[1];

        Id target{};
        if (const auto gpr = std::get_if<GprNode>(&*dest)) {
            if (gpr->GetIndex() == Register::ZeroIndex) {
                // Writing to Register::ZeroIndex is a no op
                return {};
            }
            target = registers.at(gpr->GetIndex());

        } else if (const auto abuf = std::get_if<AbufNode>(&*dest)) {
            target = [&]() -> Id {
                switch (const auto attribute = abuf->GetIndex(); attribute) {
                case Attribute::Index::Position:
                    return AccessElement(t_out_float, per_vertex, position_index,
                                         abuf->GetElement());
                case Attribute::Index::LayerViewportPointSize:
                    UNIMPLEMENTED_IF(abuf->GetElement() != 3);
                    return AccessElement(t_out_float, per_vertex, point_size_index);
                case Attribute::Index::ClipDistances0123:
                    return AccessElement(t_out_float, per_vertex, clip_distances_index,
                                         abuf->GetElement());
                case Attribute::Index::ClipDistances4567:
                    return AccessElement(t_out_float, per_vertex, clip_distances_index,
                                         abuf->GetElement() + 4);
                default:
                    if (IsGenericAttribute(attribute)) {
                        return AccessElement(t_out_float, output_attributes.at(attribute),
                                             abuf->GetElement());
                    }
                    UNIMPLEMENTED_MSG("Unhandled output attribute: {}",
                                      static_cast<u32>(attribute));
                    return {};
                }
            }();

        } else if (const auto lmem = std::get_if<LmemNode>(&*dest)) {
            Id address = BitcastTo<Type::Uint>(Visit(lmem->GetAddress()));
            address = Emit(OpUDiv(t_uint, address, Constant(t_uint, 4)));
            target = Emit(OpAccessChain(t_prv_float, local_memory, {address}));
        }

        Emit(OpStore(target, Visit(src)));
        return {};
    }

    Id FCastHalf0(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id FCastHalf1(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HNegate(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HClamp(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HCastFloat(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HUnpack(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HMergeF32(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HMergeH0(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HMergeH1(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id HPack2(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id LogicalAssign(Operation operation) {
        const Node& dest = operation[0];
        const Node& src = operation[1];

        Id target{};
        if (const auto pred = std::get_if<PredicateNode>(&*dest)) {
            ASSERT_MSG(!pred->IsNegated(), "Negating logical assignment");

            const auto index = pred->GetIndex();
            switch (index) {
            case Tegra::Shader::Pred::NeverExecute:
            case Tegra::Shader::Pred::UnusedIndex:
                // Writing to these predicates is a no-op
                return {};
            }
            target = predicates.at(index);

        } else if (const auto flag = std::get_if<InternalFlagNode>(&*dest)) {
            target = internal_flags.at(static_cast<u32>(flag->GetFlag()));
        }

        Emit(OpStore(target, Visit(src)));
        return {};
    }

    Id LogicalPick2(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id LogicalAnd2(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id GetTextureSampler(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        const auto entry = sampler_images.at(static_cast<u32>(meta->sampler.GetIndex()));
        return Emit(OpLoad(entry.sampled_image_type, entry.sampler));
    }

    Id GetTextureImage(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        const auto entry = sampler_images.at(static_cast<u32>(meta->sampler.GetIndex()));
        return Emit(OpImage(entry.image_type, GetTextureSampler(operation)));
    }

    Id GetTextureCoordinates(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        std::vector<Id> coords;
        for (std::size_t i = 0; i < operation.GetOperandsCount(); ++i) {
            coords.push_back(Visit(operation[i]));
        }
        if (meta->sampler.IsArray()) {
            const Id array_integer = BitcastTo<Type::Int>(Visit(meta->array));
            coords.push_back(Emit(OpConvertSToF(t_float, array_integer)));
        }
        if (meta->sampler.IsShadow()) {
            coords.push_back(Visit(meta->depth_compare));
        }

        const std::array<Id, 4> t_float_lut = {nullptr, t_float2, t_float3, t_float4};
        return coords.size() == 1
                   ? coords[0]
                   : Emit(OpCompositeConstruct(t_float_lut.at(coords.size() - 1), coords));
    }

    Id GetTextureElement(Operation operation, Id sample_value) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        ASSERT(meta);
        return Emit(OpCompositeExtract(t_float, sample_value, meta->element));
    }

    Id Texture(Operation operation) {
        const Id texture = Emit(OpImageSampleImplicitLod(t_float4, GetTextureSampler(operation),
                                                         GetTextureCoordinates(operation)));
        return GetTextureElement(operation, texture);
    }

    Id TextureLod(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        const Id texture = Emit(OpImageSampleExplicitLod(
            t_float4, GetTextureSampler(operation), GetTextureCoordinates(operation),
            spv::ImageOperandsMask::Lod, Visit(meta->lod)));
        return GetTextureElement(operation, texture);
    }

    Id TextureGather(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        const auto coords = GetTextureCoordinates(operation);

        Id texture;
        if (meta->sampler.IsShadow()) {
            texture = Emit(OpImageDrefGather(t_float4, GetTextureSampler(operation), coords,
                                             Visit(meta->component)));
        } else {
            u32 component_value = 0;
            if (meta->component) {
                const auto component = std::get_if<ImmediateNode>(&*meta->component);
                ASSERT_MSG(component, "Component is not an immediate value");
                component_value = component->GetValue();
            }
            texture = Emit(OpImageGather(t_float4, GetTextureSampler(operation), coords,
                                         Constant(t_uint, component_value)));
        }

        return GetTextureElement(operation, texture);
    }

    Id TextureQueryDimensions(Operation operation) {
        const auto meta = std::get_if<MetaTexture>(&operation.GetMeta());
        const auto image_id = GetTextureImage(operation);
        AddCapability(spv::Capability::ImageQuery);

        if (meta->element == 3) {
            return BitcastTo<Type::Float>(Emit(OpImageQueryLevels(t_int, image_id)));
        }

        const Id lod = VisitOperand<Type::Uint>(operation, 0);
        const std::size_t coords_count = [&]() {
            switch (const auto type = meta->sampler.GetType(); type) {
            case Tegra::Shader::TextureType::Texture1D:
                return 1;
            case Tegra::Shader::TextureType::Texture2D:
            case Tegra::Shader::TextureType::TextureCube:
                return 2;
            case Tegra::Shader::TextureType::Texture3D:
                return 3;
            default:
                UNREACHABLE_MSG("Invalid texture type={}", static_cast<u32>(type));
                return 2;
            }
        }();

        if (meta->element >= coords_count) {
            return Constant(t_float, 0.0f);
        }

        const std::array<Id, 3> types = {t_int, t_int2, t_int3};
        const Id sizes = Emit(OpImageQuerySizeLod(types.at(coords_count - 1), image_id, lod));
        const Id size = Emit(OpCompositeExtract(t_int, sizes, meta->element));
        return BitcastTo<Type::Float>(size);
    }

    Id TextureQueryLod(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id TexelFetch(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ImageLoad(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ImageStore(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id AtomicImageAdd(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id AtomicImageAnd(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id AtomicImageOr(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id AtomicImageXor(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id AtomicImageExchange(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id Branch(Operation operation) {
        const auto target = std::get_if<ImmediateNode>(&*operation[0]);
        UNIMPLEMENTED_IF(!target);

        Emit(OpStore(jmp_to, Constant(t_uint, target->GetValue())));
        BranchingOp([&]() { Emit(OpBranch(continue_label)); });
        return {};
    }

    Id BranchIndirect(Operation operation) {
        const Id op_a = VisitOperand<Type::Uint>(operation, 0);

        Emit(OpStore(jmp_to, op_a));
        BranchingOp([&]() { Emit(OpBranch(continue_label)); });
        return {};
    }

    Id PushFlowStack(Operation operation) {
        const auto target = std::get_if<ImmediateNode>(&*operation[0]);
        ASSERT(target);

        const auto [flow_stack, flow_stack_top] = GetFlowStack(operation);
        const Id current = Emit(OpLoad(t_uint, flow_stack_top));
        const Id next = Emit(OpIAdd(t_uint, current, Constant(t_uint, 1)));
        const Id access = Emit(OpAccessChain(t_func_uint, flow_stack, current));

        Emit(OpStore(access, Constant(t_uint, target->GetValue())));
        Emit(OpStore(flow_stack_top, next));
        return {};
    }

    Id PopFlowStack(Operation operation) {
        const auto [flow_stack, flow_stack_top] = GetFlowStack(operation);
        const Id current = Emit(OpLoad(t_uint, flow_stack_top));
        const Id previous = Emit(OpISub(t_uint, current, Constant(t_uint, 1)));
        const Id access = Emit(OpAccessChain(t_func_uint, flow_stack, previous));
        const Id target = Emit(OpLoad(t_uint, access));

        Emit(OpStore(flow_stack_top, previous));
        Emit(OpStore(jmp_to, target));
        BranchingOp([&]() { Emit(OpBranch(continue_label)); });
        return {};
    }

    Id Exit(Operation operation) {
        switch (stage) {
        case ShaderStage::Vertex: {
            // TODO(Rodrigo): We should use VK_EXT_depth_range_unrestricted instead, but it doesn't
            // seem to be working on Nvidia's drivers and Intel (mesa and blob) doesn't support it.
            const Id z_pointer = AccessElement(t_out_float, per_vertex, position_index, 2u);
            Id depth = Emit(OpLoad(t_float, z_pointer));
            depth = Emit(OpFAdd(t_float, depth, Constant(t_float, 1.0f)));
            depth = Emit(OpFMul(t_float, depth, Constant(t_float, 0.5f)));
            Emit(OpStore(z_pointer, depth));
            break;
        }
        case ShaderStage::Fragment: {
            const auto SafeGetRegister = [&](u32 reg) {
                // TODO(Rodrigo): Replace with contains once C++20 releases
                if (const auto it = registers.find(reg); it != registers.end()) {
                    return Emit(OpLoad(t_float, it->second));
                }
                return Constant(t_float, 0.0f);
            };

            UNIMPLEMENTED_IF_MSG(header.ps.omap.sample_mask != 0,
                                 "Sample mask write is unimplemented");

            // TODO(Rodrigo): Alpha testing

            // Write the color outputs using the data in the shader registers, disabled
            // rendertargets/components are skipped in the register assignment.
            u32 current_reg = 0;
            for (u32 rt = 0; rt < Maxwell::NumRenderTargets; ++rt) {
                // TODO(Subv): Figure out how dual-source blending is configured in the Switch.
                for (u32 component = 0; component < 4; ++component) {
                    if (header.ps.IsColorComponentOutputEnabled(rt, component)) {
                        Emit(OpStore(AccessElement(t_out_float, frag_colors.at(rt), component),
                                     SafeGetRegister(current_reg)));
                        ++current_reg;
                    }
                }
            }
            if (header.ps.omap.depth) {
                // The depth output is always 2 registers after the last color output, and
                // current_reg already contains one past the last color register.
                Emit(OpStore(frag_depth, SafeGetRegister(current_reg + 1)));
            }
            break;
        }
        }

        BranchingOp([&]() { Emit(OpReturn()); });
        return {};
    }

    Id Discard(Operation operation) {
        BranchingOp([&]() { Emit(OpKill()); });
        return {};
    }

    Id EmitVertex(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id EndPrimitive(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id YNegate(Operation operation) {
        UNIMPLEMENTED();
        return {};
    }

    template <u32 element>
    Id LocalInvocationId(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    template <u32 element>
    Id WorkGroupId(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id BallotThread(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id VoteAll(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id VoteAny(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id VoteEqual(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ShuffleIndexed(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ShuffleUp(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ShuffleDown(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id ShuffleButterfly(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id InRangeShuffleIndexed(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id InRangeShuffleUp(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id InRangeShuffleDown(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id InRangeShuffleButterfly(Operation) {
        UNIMPLEMENTED();
        return {};
    }

    Id DeclareBuiltIn(spv::BuiltIn builtin, spv::StorageClass storage, Id type,
                      const std::string& name) {
        const Id id = OpVariable(type, storage);
        Decorate(id, spv::Decoration::BuiltIn, static_cast<u32>(builtin));
        AddGlobalVariable(Name(id, name));
        interfaces.push_back(id);
        return id;
    }

    bool IsRenderTargetUsed(u32 rt) const {
        for (u32 component = 0; component < 4; ++component) {
            if (header.ps.IsColorComponentOutputEnabled(rt, component)) {
                return true;
            }
        }
        return false;
    }

    template <typename... Args>
    Id AccessElement(Id pointer_type, Id composite, Args... elements_) {
        std::vector<Id> members;
        auto elements = {elements_...};
        for (const auto element : elements) {
            members.push_back(Constant(t_uint, element));
        }

        return Emit(OpAccessChain(pointer_type, composite, members));
    }

    template <Type type>
    Id VisitOperand(Operation operation, std::size_t operand_index) {
        const Id value = Visit(operation[operand_index]);

        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            return value;
        case Type::Int:
            return Emit(OpBitcast(t_int, value));
        case Type::Uint:
            return Emit(OpBitcast(t_uint, value));
        case Type::HalfFloat:
            UNIMPLEMENTED();
        }
        UNREACHABLE();
        return value;
    }

    template <Type type>
    Id BitcastFrom(Id value) {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
        case Type::Float:
            return value;
        case Type::Int:
        case Type::Uint:
            return Emit(OpBitcast(t_float, value));
        case Type::HalfFloat:
            UNIMPLEMENTED();
        }
        UNREACHABLE();
        return value;
    }

    template <Type type>
    Id BitcastTo(Id value) {
        switch (type) {
        case Type::Bool:
        case Type::Bool2:
            UNREACHABLE();
        case Type::Float:
            return Emit(OpBitcast(t_float, value));
        case Type::Int:
            return Emit(OpBitcast(t_int, value));
        case Type::Uint:
            return Emit(OpBitcast(t_uint, value));
        case Type::HalfFloat:
            UNIMPLEMENTED();
        }
        UNREACHABLE();
        return value;
    }

    Id GetTypeDefinition(Type type) {
        switch (type) {
        case Type::Bool:
            return t_bool;
        case Type::Bool2:
            return t_bool2;
        case Type::Float:
            return t_float;
        case Type::Int:
            return t_int;
        case Type::Uint:
            return t_uint;
        case Type::HalfFloat:
            UNIMPLEMENTED();
        }
        UNREACHABLE();
        return {};
    }

    void BranchingOp(std::function<void()> call) {
        const Id true_label = OpLabel();
        const Id skip_label = OpLabel();
        Emit(OpSelectionMerge(skip_label, spv::SelectionControlMask::Flatten));
        Emit(OpBranchConditional(v_true, true_label, skip_label, 1, 0));
        Emit(true_label);
        call();

        Emit(skip_label);
    }

    std::tuple<Id, Id> CreateFlowStack() {
        // TODO(Rodrigo): Figure out the actual depth of the flow stack, for now it seems unlikely
        // that shaders will use 20 nested SSYs and PBKs.
        constexpr u32 FLOW_STACK_SIZE = 20;
        constexpr auto storage_class = spv::StorageClass::Function;

        const Id flow_stack_type = TypeArray(t_uint, Constant(t_uint, FLOW_STACK_SIZE));
        const Id stack = Emit(OpVariable(TypePointer(storage_class, flow_stack_type), storage_class,
                                         ConstantNull(flow_stack_type)));
        const Id top = Emit(OpVariable(t_func_uint, storage_class, Constant(t_uint, 0)));
        return std::tie(stack, top);
    }

    std::pair<Id, Id> GetFlowStack(Operation operation) {
        const auto stack_class = std::get<MetaStackClass>(operation.GetMeta());
        switch (stack_class) {
        case MetaStackClass::Ssy:
            return {ssy_flow_stack, ssy_flow_stack_top};
        case MetaStackClass::Pbk:
            return {pbk_flow_stack, pbk_flow_stack_top};
        }
        UNREACHABLE();
        return {};
    }

    static constexpr std::array operation_decompilers = {
        &SPIRVDecompiler::Assign,

        &SPIRVDecompiler::Ternary<&Module::OpSelect, Type::Float, Type::Bool, Type::Float,
                                  Type::Float>,

        &SPIRVDecompiler::Binary<&Module::OpFAdd, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFMul, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFDiv, Type::Float>,
        &SPIRVDecompiler::Ternary<&Module::OpFma, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpFNegate, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpFAbs, Type::Float>,
        &SPIRVDecompiler::Ternary<&Module::OpFClamp, Type::Float>,
        &SPIRVDecompiler::FCastHalf0,
        &SPIRVDecompiler::FCastHalf1,
        &SPIRVDecompiler::Binary<&Module::OpFMin, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFMax, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpCos, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpSin, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpExp2, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpLog2, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpInverseSqrt, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpSqrt, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpRoundEven, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpFloor, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpCeil, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpTrunc, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpConvertSToF, Type::Float, Type::Int>,
        &SPIRVDecompiler::Unary<&Module::OpConvertUToF, Type::Float, Type::Uint>,

        &SPIRVDecompiler::Binary<&Module::OpIAdd, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpIMul, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSDiv, Type::Int>,
        &SPIRVDecompiler::Unary<&Module::OpSNegate, Type::Int>,
        &SPIRVDecompiler::Unary<&Module::OpSAbs, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSMin, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSMax, Type::Int>,

        &SPIRVDecompiler::Unary<&Module::OpConvertFToS, Type::Int, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpBitcast, Type::Int, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpShiftLeftLogical, Type::Int, Type::Int, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpShiftRightLogical, Type::Int, Type::Int, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpShiftRightArithmetic, Type::Int, Type::Int, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseAnd, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseOr, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseXor, Type::Int>,
        &SPIRVDecompiler::Unary<&Module::OpNot, Type::Int>,
        &SPIRVDecompiler::Quaternary<&Module::OpBitFieldInsert, Type::Int>,
        &SPIRVDecompiler::Ternary<&Module::OpBitFieldSExtract, Type::Int>,
        &SPIRVDecompiler::Unary<&Module::OpBitCount, Type::Int>,

        &SPIRVDecompiler::Binary<&Module::OpIAdd, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpIMul, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpUDiv, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpUMin, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpUMax, Type::Uint>,
        &SPIRVDecompiler::Unary<&Module::OpConvertFToU, Type::Uint, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpBitcast, Type::Uint, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpShiftLeftLogical, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpShiftRightLogical, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpShiftRightArithmetic, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseAnd, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseOr, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpBitwiseXor, Type::Uint>,
        &SPIRVDecompiler::Unary<&Module::OpNot, Type::Uint>,
        &SPIRVDecompiler::Quaternary<&Module::OpBitFieldInsert, Type::Uint>,
        &SPIRVDecompiler::Ternary<&Module::OpBitFieldUExtract, Type::Uint>,
        &SPIRVDecompiler::Unary<&Module::OpBitCount, Type::Uint>,

        &SPIRVDecompiler::Binary<&Module::OpFAdd, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFMul, Type::HalfFloat>,
        &SPIRVDecompiler::Ternary<&Module::OpFma, Type::HalfFloat>,
        &SPIRVDecompiler::Unary<&Module::OpFAbs, Type::HalfFloat>,
        &SPIRVDecompiler::HNegate,
        &SPIRVDecompiler::HClamp,
        &SPIRVDecompiler::HCastFloat,
        &SPIRVDecompiler::HUnpack,
        &SPIRVDecompiler::HMergeF32,
        &SPIRVDecompiler::HMergeH0,
        &SPIRVDecompiler::HMergeH1,
        &SPIRVDecompiler::HPack2,

        &SPIRVDecompiler::LogicalAssign,
        &SPIRVDecompiler::Binary<&Module::OpLogicalAnd, Type::Bool>,
        &SPIRVDecompiler::Binary<&Module::OpLogicalOr, Type::Bool>,
        &SPIRVDecompiler::Binary<&Module::OpLogicalNotEqual, Type::Bool>,
        &SPIRVDecompiler::Unary<&Module::OpLogicalNot, Type::Bool>,
        &SPIRVDecompiler::LogicalPick2,
        &SPIRVDecompiler::LogicalAnd2,

        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThan, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdEqual, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThanEqual, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThan, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdNotEqual, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThanEqual, Type::Bool, Type::Float>,
        &SPIRVDecompiler::Unary<&Module::OpIsNan, Type::Bool>,

        &SPIRVDecompiler::Binary<&Module::OpSLessThan, Type::Bool, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpIEqual, Type::Bool, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSLessThanEqual, Type::Bool, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSGreaterThan, Type::Bool, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpINotEqual, Type::Bool, Type::Int>,
        &SPIRVDecompiler::Binary<&Module::OpSGreaterThanEqual, Type::Bool, Type::Int>,

        &SPIRVDecompiler::Binary<&Module::OpULessThan, Type::Bool, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpIEqual, Type::Bool, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpULessThanEqual, Type::Bool, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpUGreaterThan, Type::Bool, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpINotEqual, Type::Bool, Type::Uint>,
        &SPIRVDecompiler::Binary<&Module::OpUGreaterThanEqual, Type::Bool, Type::Uint>,

        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThan, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThanEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThan, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdNotEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThanEqual, Type::Bool, Type::HalfFloat>,
        // TODO(Rodrigo): Should these use the OpFUnord* variants?
        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThan, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdLessThanEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThan, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdNotEqual, Type::Bool, Type::HalfFloat>,
        &SPIRVDecompiler::Binary<&Module::OpFOrdGreaterThanEqual, Type::Bool, Type::HalfFloat>,

        &SPIRVDecompiler::Texture,
        &SPIRVDecompiler::TextureLod,
        &SPIRVDecompiler::TextureGather,
        &SPIRVDecompiler::TextureQueryDimensions,
        &SPIRVDecompiler::TextureQueryLod,
        &SPIRVDecompiler::TexelFetch,

        &SPIRVDecompiler::ImageLoad,
        &SPIRVDecompiler::ImageStore,
        &SPIRVDecompiler::AtomicImageAdd,
        &SPIRVDecompiler::AtomicImageAnd,
        &SPIRVDecompiler::AtomicImageOr,
        &SPIRVDecompiler::AtomicImageXor,
        &SPIRVDecompiler::AtomicImageExchange,

        &SPIRVDecompiler::Branch,
        &SPIRVDecompiler::BranchIndirect,
        &SPIRVDecompiler::PushFlowStack,
        &SPIRVDecompiler::PopFlowStack,
        &SPIRVDecompiler::Exit,
        &SPIRVDecompiler::Discard,

        &SPIRVDecompiler::EmitVertex,
        &SPIRVDecompiler::EndPrimitive,

        &SPIRVDecompiler::YNegate,
        &SPIRVDecompiler::LocalInvocationId<0>,
        &SPIRVDecompiler::LocalInvocationId<1>,
        &SPIRVDecompiler::LocalInvocationId<2>,
        &SPIRVDecompiler::WorkGroupId<0>,
        &SPIRVDecompiler::WorkGroupId<1>,
        &SPIRVDecompiler::WorkGroupId<2>,

        &SPIRVDecompiler::BallotThread,
        &SPIRVDecompiler::VoteAll,
        &SPIRVDecompiler::VoteAny,
        &SPIRVDecompiler::VoteEqual,

        &SPIRVDecompiler::ShuffleIndexed,
        &SPIRVDecompiler::ShuffleUp,
        &SPIRVDecompiler::ShuffleDown,
        &SPIRVDecompiler::ShuffleButterfly,

        &SPIRVDecompiler::InRangeShuffleIndexed,
        &SPIRVDecompiler::InRangeShuffleUp,
        &SPIRVDecompiler::InRangeShuffleDown,
        &SPIRVDecompiler::InRangeShuffleButterfly,
    };
    static_assert(operation_decompilers.size() == static_cast<std::size_t>(OperationCode::Amount));

    const VKDevice& device;
    const ShaderIR& ir;
    const ShaderStage stage;
    const Tegra::Shader::Header header;

    const Id t_void = Name(TypeVoid(), "void");

    const Id t_bool = Name(TypeBool(), "bool");
    const Id t_bool2 = Name(TypeVector(t_bool, 2), "bool2");

    const Id t_int = Name(TypeInt(32, true), "int");
    const Id t_int2 = Name(TypeVector(t_int, 2), "int2");
    const Id t_int3 = Name(TypeVector(t_int, 3), "int3");
    const Id t_int4 = Name(TypeVector(t_int, 4), "int4");

    const Id t_uint = Name(TypeInt(32, false), "uint");
    const Id t_uint2 = Name(TypeVector(t_uint, 2), "uint2");
    const Id t_uint3 = Name(TypeVector(t_uint, 3), "uint3");
    const Id t_uint4 = Name(TypeVector(t_uint, 4), "uint4");

    const Id t_float = Name(TypeFloat(32), "float");
    const Id t_float2 = Name(TypeVector(t_float, 2), "float2");
    const Id t_float3 = Name(TypeVector(t_float, 3), "float3");
    const Id t_float4 = Name(TypeVector(t_float, 4), "float4");

    const Id t_prv_bool = Name(TypePointer(spv::StorageClass::Private, t_bool), "prv_bool");
    const Id t_prv_float = Name(TypePointer(spv::StorageClass::Private, t_float), "prv_float");

    const Id t_func_uint = Name(TypePointer(spv::StorageClass::Function, t_uint), "func_uint");

    const Id t_in_bool = Name(TypePointer(spv::StorageClass::Input, t_bool), "in_bool");
    const Id t_in_uint = Name(TypePointer(spv::StorageClass::Input, t_uint), "in_uint");
    const Id t_in_float = Name(TypePointer(spv::StorageClass::Input, t_float), "in_float");
    const Id t_in_float4 = Name(TypePointer(spv::StorageClass::Input, t_float4), "in_float4");

    const Id t_out_float = Name(TypePointer(spv::StorageClass::Output, t_float), "out_float");
    const Id t_out_float4 = Name(TypePointer(spv::StorageClass::Output, t_float4), "out_float4");

    const Id t_cbuf_float = TypePointer(spv::StorageClass::Uniform, t_float);
    const Id t_cbuf_std140 = Decorate(
        Name(TypeArray(t_float4, Constant(t_uint, MAX_CONSTBUFFER_ELEMENTS)), "CbufStd140Array"),
        spv::Decoration::ArrayStride, 16u);
    const Id t_cbuf_scalar = Decorate(
        Name(TypeArray(t_float, Constant(t_uint, MAX_CONSTBUFFER_FLOATS)), "CbufScalarArray"),
        spv::Decoration::ArrayStride, 4u);
    const Id t_cbuf_std140_struct = MemberDecorate(
        Decorate(TypeStruct(t_cbuf_std140), spv::Decoration::Block), 0, spv::Decoration::Offset, 0);
    const Id t_cbuf_scalar_struct = MemberDecorate(
        Decorate(TypeStruct(t_cbuf_scalar), spv::Decoration::Block), 0, spv::Decoration::Offset, 0);
    const Id t_cbuf_std140_ubo = TypePointer(spv::StorageClass::Uniform, t_cbuf_std140_struct);
    const Id t_cbuf_scalar_ubo = TypePointer(spv::StorageClass::Uniform, t_cbuf_scalar_struct);

    const Id t_gmem_float = TypePointer(spv::StorageClass::StorageBuffer, t_float);
    const Id t_gmem_array =
        Name(Decorate(TypeRuntimeArray(t_float), spv::Decoration::ArrayStride, 4u), "GmemArray");
    const Id t_gmem_struct = MemberDecorate(
        Decorate(TypeStruct(t_gmem_array), spv::Decoration::Block), 0, spv::Decoration::Offset, 0);
    const Id t_gmem_ssbo = TypePointer(spv::StorageClass::StorageBuffer, t_gmem_struct);

    const Id v_float_zero = Constant(t_float, 0.0f);
    const Id v_true = ConstantTrue(t_bool);
    const Id v_false = ConstantFalse(t_bool);

    Id per_vertex{};
    std::map<u32, Id> registers;
    std::map<Tegra::Shader::Pred, Id> predicates;
    Id local_memory{};
    std::array<Id, INTERNAL_FLAGS_COUNT> internal_flags{};
    std::map<Attribute::Index, Id> input_attributes;
    std::map<Attribute::Index, Id> output_attributes;
    std::map<u32, Id> constant_buffers;
    std::map<GlobalMemoryBase, Id> global_buffers;
    std::map<u32, SamplerImage> sampler_images;

    Id instance_index{};
    Id vertex_index{};
    std::array<Id, Maxwell::NumRenderTargets> frag_colors{};
    Id frag_depth{};
    Id frag_coord{};
    Id front_facing{};

    u32 position_index{};
    u32 point_size_index{};
    u32 clip_distances_index{};

    std::vector<Id> interfaces;

    u32 const_buffers_base_binding{};
    u32 global_buffers_base_binding{};
    u32 samplers_base_binding{};

    Id execute_function{};
    Id jmp_to{};
    Id ssy_flow_stack_top{};
    Id pbk_flow_stack_top{};
    Id ssy_flow_stack{};
    Id pbk_flow_stack{};
    Id continue_label{};
    std::map<u32, Id> labels;
};

DecompilerResult Decompile(const VKDevice& device, const VideoCommon::Shader::ShaderIR& ir,
                           Maxwell::ShaderStage stage) {
    auto decompiler = std::make_unique<SPIRVDecompiler>(device, ir, stage);
    decompiler->Decompile();
    return {std::move(decompiler), decompiler->GetShaderEntries()};
}

} // namespace Vulkan::VKShader
