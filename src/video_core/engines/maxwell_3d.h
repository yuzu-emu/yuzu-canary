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
#include "common/math_util.h"
#include "video_core/gpu.h"
#include "video_core/macro_interpreter.h"
#include "video_core/memory_manager.h"
#include "video_core/textures/texture.h"

namespace Tegra {
namespace Engines {

#define MAXWELL3D_REG_INDEX(field_name)                                                            \
    (offsetof(Tegra::Engines::Maxwell3D::Regs, field_name) / sizeof(u32))

class Maxwell3D final {
public:
    explicit Maxwell3D(MemoryManager& memory_manager);
    ~Maxwell3D() = default;

    /// Register structure of the Maxwell3D engine.
    /// TODO(Subv): This structure will need to be made bigger as more registers are discovered.
    struct Regs {
        static constexpr size_t NUM_REGS = 0xE00;

        static constexpr size_t NumRenderTargets = 8;
        static constexpr size_t NumViewports = 16;
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
            // TODO(Subv): It is currently unknown what the difference between method 2 and method 0
            // is.
            Write2 = 2,
        };

        enum class QueryUnit : u32 {
            VFetch = 1,
            VP = 2,
            Rast = 4,
            StrmOut = 5,
            GP = 6,
            ZCull = 7,
            Prop = 10,
            Crop = 15,
        };

        enum class QuerySelect : u32 {
            Zero = 0,
        };

        enum class QuerySyncCondition : u32 {
            NotEqual = 0,
            GreaterThan = 1,
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

        struct VertexAttribute {
            enum class Size : u32 {
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

            enum class Type : u32 {
                SignedNorm = 1,
                UnsignedNorm = 2,
                SignedInt = 3,
                UnsignedInt = 4,
                UnsignedScaled = 5,
                SignedScaled = 6,
                Float = 7,
            };

            union {
                BitField<0, 5, u32> buffer;
                BitField<6, 1, u32> constant;
                BitField<7, 14, u32> offset;
                BitField<21, 6, Size> size;
                BitField<27, 3, Type> type;
                BitField<31, 1, u32> bgra;
            };

            u32 ComponentCount() const {
                switch (size) {
                case Size::Size_32_32_32_32:
                    return 4;
                case Size::Size_32_32_32:
                    return 3;
                case Size::Size_16_16_16_16:
                    return 4;
                case Size::Size_32_32:
                    return 2;
                case Size::Size_16_16_16:
                    return 3;
                case Size::Size_8_8_8_8:
                    return 4;
                case Size::Size_16_16:
                    return 2;
                case Size::Size_32:
                    return 1;
                case Size::Size_8_8_8:
                    return 3;
                case Size::Size_8_8:
                    return 2;
                case Size::Size_16:
                    return 1;
                case Size::Size_8:
                    return 1;
                case Size::Size_10_10_10_2:
                    return 4;
                case Size::Size_11_11_10:
                    return 3;
                default:
                    UNREACHABLE();
                }
            }

            u32 SizeInBytes() const {
                switch (size) {
                case Size::Size_32_32_32_32:
                    return 16;
                case Size::Size_32_32_32:
                    return 12;
                case Size::Size_16_16_16_16:
                    return 8;
                case Size::Size_32_32:
                    return 8;
                case Size::Size_16_16_16:
                    return 6;
                case Size::Size_8_8_8_8:
                    return 4;
                case Size::Size_16_16:
                    return 4;
                case Size::Size_32:
                    return 4;
                case Size::Size_8_8_8:
                    return 3;
                case Size::Size_8_8:
                    return 2;
                case Size::Size_16:
                    return 2;
                case Size::Size_8:
                    return 1;
                case Size::Size_10_10_10_2:
                    return 4;
                case Size::Size_11_11_10:
                    return 4;
                default:
                    UNREACHABLE();
                }
            }

            std::string SizeString() const {
                switch (size) {
                case Size::Size_32_32_32_32:
                    return "32_32_32_32";
                case Size::Size_32_32_32:
                    return "32_32_32";
                case Size::Size_16_16_16_16:
                    return "16_16_16_16";
                case Size::Size_32_32:
                    return "32_32";
                case Size::Size_16_16_16:
                    return "16_16_16";
                case Size::Size_8_8_8_8:
                    return "8_8_8_8";
                case Size::Size_16_16:
                    return "16_16";
                case Size::Size_32:
                    return "32";
                case Size::Size_8_8_8:
                    return "8_8_8";
                case Size::Size_8_8:
                    return "8_8";
                case Size::Size_16:
                    return "16";
                case Size::Size_8:
                    return "8";
                case Size::Size_10_10_10_2:
                    return "10_10_10_2";
                case Size::Size_11_11_10:
                    return "11_11_10";
                }
                UNREACHABLE();
                return {};
            }

            std::string TypeString() const {
                switch (type) {
                case Type::SignedNorm:
                    return "SNORM";
                case Type::UnsignedNorm:
                    return "UNORM";
                case Type::SignedInt:
                    return "SINT";
                case Type::UnsignedInt:
                    return "UINT";
                case Type::UnsignedScaled:
                    return "USCALED";
                case Type::SignedScaled:
                    return "SSCALED";
                case Type::Float:
                    return "FLOAT";
                }
                UNREACHABLE();
                return {};
            }

            bool IsNormalized() const {
                return (type == Type::SignedNorm) || (type == Type::UnsignedNorm);
            }
        };

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

        enum class IndexFormat : u32 {
            UnsignedByte = 0x0,
            UnsignedShort = 0x1,
            UnsignedInt = 0x2,
        };

        enum class ComparisonOp : u32 {
            // These values are used by Nouveau and most games, they correspond to the OpenGL token
            // values for these operations.
            Never = 0x200,
            Less = 0x201,
            Equal = 0x202,
            LessEqual = 0x203,
            Greater = 0x204,
            NotEqual = 0x205,
            GreaterEqual = 0x206,
            Always = 0x207,

            // These values are used by some games, they seem to be NV04 values.
            NeverOld = 1,
            LessOld = 2,
            EqualOld = 3,
            LessEqualOld = 4,
            GreaterOld = 5,
            NotEqualOld = 6,
            GreaterEqualOld = 7,
            AlwaysOld = 8,
        };

        struct Cull {
            enum class FrontFace : u32 {
                ClockWise = 0x0900,
                CounterClockWise = 0x0901,
            };

            enum class CullFace : u32 {
                Front = 0x0404,
                Back = 0x0405,
                FrontAndBack = 0x0408,
            };

            u32 enabled;
            FrontFace front_face;
            CullFace cull_face;
        };

        struct Blend {
            enum class Equation : u32 {
                Add = 1,
                Subtract = 2,
                ReverseSubtract = 3,
                Min = 4,
                Max = 5,
            };

            enum class Factor : u32 {
                Zero = 0x1,
                One = 0x2,
                SourceColor = 0x3,
                OneMinusSourceColor = 0x4,
                SourceAlpha = 0x5,
                OneMinusSourceAlpha = 0x6,
                DestAlpha = 0x7,
                OneMinusDestAlpha = 0x8,
                DestColor = 0x9,
                OneMinusDestColor = 0xa,
                SourceAlphaSaturate = 0xb,
                Source1Color = 0x10,
                OneMinusSource1Color = 0x11,
                Source1Alpha = 0x12,
                OneMinusSource1Alpha = 0x13,
                ConstantColor = 0x61,
                OneMinusConstantColor = 0x62,
                ConstantAlpha = 0x63,
                OneMinusConstantAlpha = 0x64,
            };

            u32 separate_alpha;
            Equation equation_rgb;
            Factor factor_source_rgb;
            Factor factor_dest_rgb;
            Equation equation_a;
            Factor factor_source_a;
            Factor factor_dest_a;
            INSERT_PADDING_WORDS(1);
        };

        struct RenderTargetConfig {
            u32 address_high;
            u32 address_low;
            u32 width;
            u32 height;
            Tegra::RenderTargetFormat format;
            u32 block_dimensions;
            u32 array_mode;
            u32 layer_stride;
            u32 base_layer;
            INSERT_PADDING_WORDS(7);

            GPUVAddr Address() const {
                return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                             address_low);
            }
        };

        bool IsShaderConfigEnabled(size_t index) const {
            // The VertexB is always enabled.
            if (index == static_cast<size_t>(Regs::ShaderProgram::VertexB)) {
                return true;
            }
            return shader_config[index].enable != 0;
        }

        union {
            struct {
                INSERT_PADDING_WORDS(0x45);

                struct {
                    INSERT_PADDING_WORDS(1);
                    u32 data;
                    u32 entry;
                } macros;

                INSERT_PADDING_WORDS(0x1B8);

                RenderTargetConfig rt[NumRenderTargets];

                struct {
                    f32 scale_x;
                    f32 scale_y;
                    f32 scale_z;
                    f32 translate_x;
                    f32 translate_y;
                    f32 translate_z;
                    INSERT_PADDING_WORDS(2);

                    MathUtil::Rectangle<s32> GetRect() const {
                        return {
                            GetX(),               // left
                            GetY() + GetHeight(), // top
                            GetX() + GetWidth(),  // right
                            GetY()                // bottom
                        };
                    };

                    s32 GetX() const {
                        return static_cast<s32>(std::max(0.0f, translate_x - std::fabs(scale_x)));
                    }

                    s32 GetY() const {
                        return static_cast<s32>(std::max(0.0f, translate_y - std::fabs(scale_y)));
                    }

                    s32 GetWidth() const {
                        return static_cast<s32>(translate_x + std::fabs(scale_x)) - GetX();
                    }

                    s32 GetHeight() const {
                        return static_cast<s32>(translate_y + std::fabs(scale_y)) - GetY();
                    }
                } viewport_transform[NumViewports];

                struct {
                    union {
                        BitField<0, 16, u32> x;
                        BitField<16, 16, u32> width;
                    };
                    union {
                        BitField<0, 16, u32> y;
                        BitField<16, 16, u32> height;
                    };
                    float depth_range_near;
                    float depth_range_far;
                } viewport[NumViewports];

                INSERT_PADDING_WORDS(0x1D);

                struct {
                    u32 first;
                    u32 count;
                } vertex_buffer;

                INSERT_PADDING_WORDS(1);

                float clear_color[4];
                float clear_depth;

                INSERT_PADDING_WORDS(0x93);

                struct {
                    u32 address_high;
                    u32 address_low;
                    Tegra::DepthFormat format;
                    u32 block_dimensions;
                    u32 layer_stride;

                    GPUVAddr Address() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) |
                                                     address_low);
                    }
                } zeta;

                INSERT_PADDING_WORDS(0x5B);

                VertexAttribute vertex_attrib_format[NumVertexAttributes];

                INSERT_PADDING_WORDS(0xF);

                struct {
                    union {
                        BitField<0, 4, u32> count;
                    };
                } rt_control;

                INSERT_PADDING_WORDS(0x2B);

                u32 depth_test_enable;

                INSERT_PADDING_WORDS(0x5);

                u32 independent_blend_enable;

                u32 depth_write_enabled;

                INSERT_PADDING_WORDS(0x7);

                u32 d3d_cull_mode;

                ComparisonOp depth_test_func;

                INSERT_PADDING_WORDS(0xB);

                struct {
                    u32 separate_alpha;
                    Blend::Equation equation_rgb;
                    Blend::Factor factor_source_rgb;
                    Blend::Factor factor_dest_rgb;
                    Blend::Equation equation_a;
                    Blend::Factor factor_source_a;
                    INSERT_PADDING_WORDS(1);
                    Blend::Factor factor_dest_a;

                    u32 enable_common;
                    u32 enable[NumRenderTargets];
                } blend;

                INSERT_PADDING_WORDS(0xB);

                union {
                    BitField<4, 1, u32> triangle_rast_flip;
                } screen_y_control;

                INSERT_PADDING_WORDS(0x21);

                u32 vb_element_base;

                INSERT_PADDING_WORDS(0x49);

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

                INSERT_PADDING_WORDS(0x21);

                union {
                    BitField<2, 1, u32> coord_origin;
                    BitField<3, 10, u32> enable;
                } point_coord_replace;

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

                INSERT_PADDING_WORDS(0x6B);

                struct {
                    u32 start_addr_high;
                    u32 start_addr_low;
                    u32 end_addr_high;
                    u32 end_addr_low;
                    IndexFormat format;
                    u32 first;
                    u32 count;

                    unsigned FormatSizeInBytes() const {
                        switch (format) {
                        case IndexFormat::UnsignedByte:
                            return 1;
                        case IndexFormat::UnsignedShort:
                            return 2;
                        case IndexFormat::UnsignedInt:
                            return 4;
                        }
                        UNREACHABLE();
                    }

                    GPUVAddr StartAddress() const {
                        return static_cast<GPUVAddr>(
                            (static_cast<GPUVAddr>(start_addr_high) << 32) | start_addr_low);
                    }

                    GPUVAddr EndAddress() const {
                        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(end_addr_high) << 32) |
                                                     end_addr_low);
                    }
                } index_array;

                INSERT_PADDING_WORDS(0x7);

                INSERT_PADDING_WORDS(0x46);

                Cull cull;

                INSERT_PADDING_WORDS(0x2B);

                union {
                    u32 raw;
                    BitField<0, 1, u32> Z;
                    BitField<1, 1, u32> S;
                    BitField<2, 1, u32> R;
                    BitField<3, 1, u32> G;
                    BitField<4, 1, u32> B;
                    BitField<5, 1, u32> A;
                    BitField<6, 4, u32> RT;
                    BitField<10, 11, u32> layer;
                } clear_buffers;

                INSERT_PADDING_WORDS(0x4B);

                struct {
                    u32 query_address_high;
                    u32 query_address_low;
                    u32 query_sequence;
                    union {
                        u32 raw;
                        BitField<0, 2, QueryMode> mode;
                        BitField<4, 1, u32> fence;
                        BitField<12, 4, QueryUnit> unit;
                        BitField<16, 1, QuerySyncCondition> sync_cond;
                        BitField<23, 5, QuerySelect> select;
                        BitField<28, 1, u32> short_query;
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

                    bool IsEnabled() const {
                        return enable != 0 && StartAddress() != 0;
                    }

                } vertex_array[NumVertexArrays];

                Blend independent_blend[NumRenderTargets];

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
                    u32 offset;
                    INSERT_PADDING_WORDS(14);
                } shader_config[MaxShaderProgram];

                INSERT_PADDING_WORDS(0x80);

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

                INSERT_PADDING_WORDS(0xCC);
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

        struct ShaderStageInfo {
            std::array<ConstBufferInfo, Regs::MaxConstBuffers> const_buffers;
        };

        std::array<ShaderStageInfo, Regs::MaxShaderStage> shader_stages;
    };

    State state{};
    MemoryManager& memory_manager;

    /// Reads a register value located at the input method address
    u32 GetRegisterValue(u32 method) const;

    /// Write the value to the register identified by method.
    void WriteReg(u32 method, u32 value, u32 remaining_params);

    /// Returns a list of enabled textures for the specified shader stage.
    std::vector<Texture::FullTextureInfo> GetStageTextures(Regs::ShaderStage stage) const;

    /// Returns the texture information for a specific texture in a specific shader stage.
    Texture::FullTextureInfo GetStageTexture(Regs::ShaderStage stage, size_t offset) const;

private:
    std::unordered_map<u32, std::vector<u32>> uploaded_macros;

    /// Macro method that is currently being executed / being fed parameters.
    u32 executing_macro = 0;
    /// Parameters that have been submitted to the macro call so far.
    std::vector<u32> macro_params;

    /// Interpreter for the macro codes uploaded to the GPU.
    MacroInterpreter macro_interpreter;

    /// Retrieves information about a specific TIC entry from the TIC buffer.
    Texture::TICEntry GetTICEntry(u32 tic_index) const;

    /// Retrieves information about a specific TSC entry from the TSC buffer.
    Texture::TSCEntry GetTSCEntry(u32 tsc_index) const;

    /**
     * Call a macro on this engine.
     * @param method Method to call
     * @param parameters Arguments to the method call
     */
    void CallMacroMethod(u32 method, std::vector<u32> parameters);

    /// Handles writes to the macro uploading registers.
    void ProcessMacroUpload(u32 data);

    /// Handles a write to the CLEAR_BUFFERS register.
    void ProcessClearBuffers();

    /// Handles a write to the QUERY_GET register.
    void ProcessQueryGet();

    /// Handles a write to the CB_DATA[i] register.
    void ProcessCBData(u32 value);

    /// Handles a write to the CB_BIND register.
    void ProcessCBBind(Regs::ShaderStage stage);

    /// Handles a write to the VERTEX_END_GL register, triggering a draw.
    void DrawArrays();
};

#define ASSERT_REG_POSITION(field_name, position)                                                  \
    static_assert(offsetof(Maxwell3D::Regs, field_name) == position * 4,                           \
                  "Field " #field_name " has invalid position")

ASSERT_REG_POSITION(macros, 0x45);
ASSERT_REG_POSITION(rt, 0x200);
ASSERT_REG_POSITION(viewport_transform[0], 0x280);
ASSERT_REG_POSITION(viewport, 0x300);
ASSERT_REG_POSITION(vertex_buffer, 0x35D);
ASSERT_REG_POSITION(clear_color[0], 0x360);
ASSERT_REG_POSITION(clear_depth, 0x364);
ASSERT_REG_POSITION(zeta, 0x3F8);
ASSERT_REG_POSITION(vertex_attrib_format[0], 0x458);
ASSERT_REG_POSITION(rt_control, 0x487);
ASSERT_REG_POSITION(depth_test_enable, 0x4B3);
ASSERT_REG_POSITION(independent_blend_enable, 0x4B9);
ASSERT_REG_POSITION(depth_write_enabled, 0x4BA);
ASSERT_REG_POSITION(d3d_cull_mode, 0x4C2);
ASSERT_REG_POSITION(depth_test_func, 0x4C3);
ASSERT_REG_POSITION(blend, 0x4CF);
ASSERT_REG_POSITION(screen_y_control, 0x4EB);
ASSERT_REG_POSITION(vb_element_base, 0x50D);
ASSERT_REG_POSITION(tsc, 0x557);
ASSERT_REG_POSITION(tic, 0x55D);
ASSERT_REG_POSITION(point_coord_replace, 0x581);
ASSERT_REG_POSITION(code_address, 0x582);
ASSERT_REG_POSITION(draw, 0x585);
ASSERT_REG_POSITION(index_array, 0x5F2);
ASSERT_REG_POSITION(cull, 0x646);
ASSERT_REG_POSITION(clear_buffers, 0x674);
ASSERT_REG_POSITION(query, 0x6C0);
ASSERT_REG_POSITION(vertex_array[0], 0x700);
ASSERT_REG_POSITION(independent_blend, 0x780);
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
