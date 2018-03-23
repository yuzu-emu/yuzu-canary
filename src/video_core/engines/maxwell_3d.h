// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <unordered_map>
#include <vector>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/memory_manager.h"

namespace Tegra {
namespace Engines {

class Maxwell3D final {
public:
    explicit Maxwell3D(MemoryManager& memory_manager);
    ~Maxwell3D() = default;

    /// Write the value to the register identified by method.
    void WriteReg(u32 method, u32 value, u32 remaining_params);

    /// Uploads the code for a GPU macro program associated with the specified entry.
    void SubmitMacroCode(u32 entry, std::vector<u32> code);

    /// Register structure of the Maxwell3D engine.
    /// TODO(Subv): This structure will need to be made bigger as more registers are discovered.
    struct Regs {
        static constexpr size_t NUM_REGS = 0xE36;

        static constexpr size_t NumRenderTargets = 8;
        static constexpr size_t NumCBData = 16;
        static constexpr size_t NumVertexArrays = 32;
        static constexpr size_t NumVertexAttributes = 32;
        static constexpr size_t MaxShaderProgram = 6;
        static constexpr size_t MaxShaderStage = 5;
        // Maximum number of const buffers per shader stage.
        static constexpr size_t MaxConstBuffers = 16;

        enum class QueryMode : u32 {
            Write = 0,
            Sync = 1,
        };

        enum class ShaderProgram : u32 {
            VertexA = 0,
            VertexB = 1,
            TesselationControl = 2,
            TesselationEval = 3,
            Geometry = 4,
            Fragment = 5,
        };

        enum class ShaderStage : u32 {
            Vertex = 0,
            TesselationControl = 1,
            TesselationEval = 2,
            Geometry = 3,
            Fragment = 4,
        };

        enum class VertexSize : u32 {
            Size_32_32_32_32 = 0x01,
            Size_32_32_32 = 0x02,
            Size_16_16_16_16 = 0x03,
            Size_32_32 = 0x04,
            Size_16_16_16 = 0x05,
            Size_8_8_8_8 = 0x0a,
            Size_16_16 = 0x0f,
            Size_32 = 0x12,
            Size_8_8_8 = 0x13,
            Size_8_8 = 0x18,
            Size_16 = 0x1b,
            Size_8 = 0x1d,
            Size_10_10_10_2 = 0x30,
            Size_11_11_10 = 0x31,
        };

        static std::string VertexSizeToString(VertexSize vertex_size) {
            switch (vertex_size) {
            case VertexSize::Size_32_32_32_32:
                return "32_32_32_32";
            case VertexSize::Size_32_32_32:
                return "32_32_32";
            case VertexSize::Size_16_16_16_16:
                return "16_16_16_16";
            case VertexSize::Size_32_32:
                return "32_32";
            case VertexSize::Size_16_16_16:
                return "16_16_16";
            case VertexSize::Size_8_8_8_8:
                return "8_8_8_8";
            case VertexSize::Size_16_16:
                return "16_16";
            case VertexSize::Size_32:
                return "32";
            case VertexSize::Size_8_8_8:
                return "8_8_8";
            case VertexSize::Size_8_8:
                return "8_8";
            case VertexSize::Size_16:
                return "16";
            case VertexSize::Size_8:
                return "8";
            case VertexSize::Size_10_10_10_2:
                return "10_10_10_2";
            case VertexSize::Size_11_11_10:
                return "11_11_10";
            }
            UNIMPLEMENTED();
            return {};
        }

        enum class VertexType : u32 {
            SignedNorm = 1,
            UnsignedNorm = 2,
            SignedInt = 3,
            UnsignedInt = 4,
            UnsignedScaled = 5,
            SignedScaled = 6,
            Float = 7,
        };

        static std::string VertexTypeToString(VertexType vertex_type) {
            switch (vertex_type) {
            case VertexType::SignedNorm:
                return "SignedNorm";
            case VertexType::UnsignedNorm:
                return "UnsignedNorm";
            case VertexType::SignedInt:
                return "SignedInt";
            case VertexType::UnsignedInt:
                return "UnsignedInt";
            case VertexType::UnsignedScaled:
                return "UnsignedScaled";
            case VertexType::SignedScaled:
                return "SignedScaled";
            case VertexType::Float:
                return "Float";
            }
            UNIMPLEMENTED();
            return {};
        }

        enum class PrimitiveTopology : u32 {
            Points = 0x0,
            Lines = 0x1,
            LineLoop = 0x2,
            LineStrip = 0x3,
            Triangles = 0x4,
            TriangleStrip = 0x5,
            TriangleFan = 0x6,
            Quads = 0x7,
            QuadStrip = 0x8,
            Polygon = 0x9,
            LinesAdjacency = 0xa,
            LineStripAdjacency = 0xb,
            TrianglesAdjacency = 0xc,
            TriangleStripAdjacency = 0xd,
            Patches = 0xe,
        };

        union {
            struct {
                INSERT_PADDING_WORDS(0x200);

                struct {
                    u32 address_high;
                    u32 address_low;
                    u32 horiz;
                    u32 vert;
                    u32 format;
                    u32 block_dimensions;
                    u32 array_mode;
                    u32 layer_stride;
                    u32 base_layer;
                    INSERT_PADDING_WORDS(7);

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } rt[NumRenderTargets];

                INSERT_PADDING_WORDS(0xDD);

                struct {
                    u32 first;
                    u32 count;
                } vertex_buffer;

                INSERT_PADDING_WORDS(0x99);

                struct {
                    u32 address_high;
                    u32 address_low;
                    u32 format;
                    u32 block_dimensions;
                    u32 layer_stride;

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } zeta;

                INSERT_PADDING_WORDS(0x5B);

                union {
                    BitField<0, 5, u32> buffer;
                    BitField<6, 1, u32> constant;
                    BitField<7, 14, u32> offset;
                    BitField<21, 6, VertexSize> size;
                    BitField<27, 3, VertexType> type;
                    BitField<31, 1, u32> bgra;
                } vertex_attrib_format[NumVertexAttributes];

                INSERT_PADDING_WORDS(0xF);

                struct {
                    union {
                        BitField<0, 4, u32> count;
                    };
                } rt_control;

                INSERT_PADDING_WORDS(0xCF);

                struct {
                    u32 tsc_address_high;
                    u32 tsc_address_low;
                    u32 tsc_limit;

                    GPUVAddr TSCAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(tsc_address_high) << 32) | tsc_address_low);
                    }
                } tsc;

                INSERT_PADDING_WORDS(0x3);

                struct {
                    u32 tic_address_high;
                    u32 tic_address_low;
                    u32 tic_limit;

                    GPUVAddr TICAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(tic_address_high) << 32) | tic_address_low);
                    }
                } tic;

                INSERT_PADDING_WORDS(0x22);

                struct {
                    u32 code_address_high;
                    u32 code_address_low;

                    GPUVAddr CodeAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(code_address_high) << 32) | code_address_low);
                    }
                } code_address;
                INSERT_PADDING_WORDS(1);

                struct {
                    u32 vertex_end_gl;
                    union {
                        u32 vertex_begin_gl;
                        BitField<0, 16, PrimitiveTopology> topology;
                    };
                } draw;

                INSERT_PADDING_WORDS(0x139);
                struct {
                    u32 query_address_high;
                    u32 query_address_low;
                    u32 query_sequence;
                    union {
                        u32 raw;
                        BitField<0, 2, QueryMode> mode;
                        BitField<4, 1, u32> fence;
                        BitField<12, 4, u32> unit;
                    } query_get;

                    GPUVAddr QueryAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(query_address_high) << 32) | query_address_low);
                    }
                } query;

                INSERT_PADDING_WORDS(0x3C);

                struct {
                    union {
                        BitField<0, 12, u32> stride;
                        BitField<12, 1, u32> enable;
                    };
                    u32 start_high;
                    u32 start_low;
                    u32 divisor;

                    GPUVAddr StartAddress() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(start_high) << 32) |
                                                     start_low);
                    }
                } vertex_array[NumVertexArrays];

                INSERT_PADDING_WORDS(0x40);

                struct {
                    u32 limit_high;
                    u32 limit_low;

                    GPUVAddr LimitAddress() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(limit_high) << 32) |
                                                     limit_low);
                    }
                } vertex_array_limit[NumVertexArrays];

                struct {
                    union {
                        BitField<0, 1, u32> enable;
                        BitField<4, 4, ShaderProgram> program;
                    };
                    u32 start_id;
                    INSERT_PADDING_WORDS(1);
                    u32 gpr_alloc;
                    ShaderStage type;
                    INSERT_PADDING_WORDS(9);
                } shader_config[MaxShaderProgram];

                INSERT_PADDING_WORDS(0x8C);

                struct {
                    u32 cb_size;
                    u32 cb_address_high;
                    u32 cb_address_low;
                    u32 cb_pos;
                    u32 cb_data[NumCBData];

                    GPUVAddr BufferAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(cb_address_high) << 32) | cb_address_low);
                    }
                } const_buffer;

                INSERT_PADDING_WORDS(0x10);

                struct {
                    union {
                        u32 raw_config;
                        BitField<0, 1, u32> valid;
                        BitField<4, 5, u32> index;
                    };
                    INSERT_PADDING_WORDS(7);
                } cb_bind[MaxShaderStage];

                INSERT_PADDING_WORDS(0x56);

                u32 tex_cb_index;

                INSERT_PADDING_WORDS(0x395);

                struct {
                    /// Compressed address of a buffer that holds information about bound SSBOs.
                    /// This address is usually bound to c0 in the shaders.
                    u32 buffer_address;

                    GPUVAddr BufferAddress() const {
                        return static_cast<GPUVAddr>(buffer_address) << 8;
                    }
                } ssbo_info;

                INSERT_PADDING_WORDS(0x11);

                struct {
                    u32 address[MaxShaderStage];
                    u32 size[MaxShaderStage];
                } tex_info_buffers;

                INSERT_PADDING_WORDS(0x102);
            };
            std::array<u32, NUM_REGS> reg_array;
        };
    } regs{};

    static_assert(sizeof(Regs) == Regs::NUM_REGS * sizeof(u32), "Maxwell3D Regs has wrong size");

    struct State {
        struct ConstBufferInfo {
            GPUVAddr address;
            u32 index;
            u32 size;
            bool enabled;
        };

        struct ShaderProgramInfo {
            Regs::ShaderStage stage;
            Regs::ShaderProgram program;
            GPUVAddr address;
        };

        struct ShaderStageInfo {
            std::array<ConstBufferInfo, Regs::MaxConstBuffers> const_buffers;
        };

        std::array<ShaderStageInfo, Regs::MaxShaderStage> shader_stages;
        std::array<ShaderProgramInfo, Regs::MaxShaderProgram> shader_programs;
    };

    State state{};

private:
    MemoryManager& memory_manager;

    std::unordered_map<u32, std::vector<u32>> uploaded_macros;

    /// Macro method that is currently being executed / being fed parameters.
    u32 executing_macro = 0;
    /// Parameters that have been submitted to the macro call so far.
    std::vector<u32> macro_params;

    /**
     * Call a macro on this engine.
     * @param method Method to call
     * @param parameters Arguments to the method call
     */
    void CallMacroMethod(u32 method, const std::vector<u32>& parameters);

    /// Handles a write to the QUERY_GET register.
    void ProcessQueryGet();

    /// Handles a write to the CB_DATA[i] register.
    void ProcessCBData(u32 value);

    /// Handles a write to the CB_BIND register.
    void ProcessCBBind(Regs::ShaderStage stage);

    /// Handles a write to the VERTEX_END_GL register, triggering a draw.
    void DrawArrays();

    /// Method call handlers
    void BindTextureInfoBuffer(const std::vector<u32>& parameters);
    void SetShader(const std::vector<u32>& parameters);
    void BindStorageBuffer(const std::vector<u32>& parameters);

    struct MethodInfo {
        const char* name;
        u32 arguments;
        void (Maxwell3D::*handler)(const std::vector<u32>& parameters);
    };

    static const std::unordered_map<u32, MethodInfo> method_handlers;
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(Maxwell3D::Regs, field_name) == position * 4,                           \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(rt, 0x200);
ASSERT_REG_POSITION(vertex_buffer, 0x35D);
ASSERT_REG_POSITION(zeta, 0x3F8);
ASSERT_REG_POSITION(vertex_attrib_format[0], 0x458);
ASSERT_REG_POSITION(rt_control, 0x487);
ASSERT_REG_POSITION(tsc, 0x557);
ASSERT_REG_POSITION(tic, 0x55D);
ASSERT_REG_POSITION(code_address, 0x582);
ASSERT_REG_POSITION(draw, 0x585);
ASSERT_REG_POSITION(query, 0x6C0);
ASSERT_REG_POSITION(vertex_array[0], 0x700);
ASSERT_REG_POSITION(vertex_array_limit[0], 0x7C0);
ASSERT_REG_POSITION(shader_config[0], 0x800);
ASSERT_REG_POSITION(const_buffer, 0x8E0);
ASSERT_REG_POSITION(cb_bind[0], 0x904);
ASSERT_REG_POSITION(tex_cb_index, 0x982);
ASSERT_REG_POSITION(ssbo_info, 0xD18);
ASSERT_REG_POSITION(tex_info_buffers.address[0], 0xD2A);
ASSERT_REG_POSITION(tex_info_buffers.size[0], 0xD2F);

#undef ASSERT_REG_POSITION

} // namespace Engines
} // namespace Tegra
