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
        static constexpr size_t NUM_REGS = 0xE36;

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
        };

        union {
            struct {
                INSERT_PADDING_WORDS(0x200);

                struct {
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
                } rt[NumRenderTargets];

                f32 viewport_scale_x;
                f32 viewport_scale_y;
                f32 viewport_scale_z;
                u32 viewport_translate_x;
                u32 viewport_translate_y;
                u32 viewport_translate_z;

                INSERT_PADDING_WORDS(0x7A);

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

                    MathUtil::Rectangle<s32> GetRect() const {
                        return {
                            static_cast<s32>(x),          // left
                            static_cast<s32>(y + height), // top
                            static_cast<s32>(x + width),  // right
                            static_cast<s32>(y)           // bottom
                        };
                    };
                } viewport[NumViewports];

                INSERT_PADDING_WORDS(0x1D);

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

                VertexAttribute vertex_attrib_format[NumVertexAttributes];

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

                INSERT_PADDING_WORDS(0xC7);

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

                Blend blend;

                INSERT_PADDING_WORDS(0x39);

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

    /// Uploads the code for a GPU macro program associated with the specified entry.
    void SubmitMacroCode(u32 entry, std::vector<u32> code);

    /// Returns a list of enabled textures for the specified shader stage.
    std::vector<Texture::FullTextureInfo> GetStageTextures(Regs::ShaderStage stage) const;

    /// Returns whether the specified shader stage is enabled or not.
    bool IsShaderStageEnabled(Regs::ShaderStage stage) const;

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

ASSERT_REG_POSITION(rt, 0x200);
ASSERT_REG_POSITION(viewport_scale_x, 0x280);
ASSERT_REG_POSITION(viewport_scale_y, 0x281);
ASSERT_REG_POSITION(viewport_scale_z, 0x282);
ASSERT_REG_POSITION(viewport_translate_x, 0x283);
ASSERT_REG_POSITION(viewport_translate_y, 0x284);
ASSERT_REG_POSITION(viewport_translate_z, 0x285);
ASSERT_REG_POSITION(viewport, 0x300);
ASSERT_REG_POSITION(vertex_buffer, 0x35D);
ASSERT_REG_POSITION(zeta, 0x3F8);
ASSERT_REG_POSITION(vertex_attrib_format[0], 0x458);
ASSERT_REG_POSITION(rt_control, 0x487);
ASSERT_REG_POSITION(tsc, 0x557);
ASSERT_REG_POSITION(tic, 0x55D);
ASSERT_REG_POSITION(code_address, 0x582);
ASSERT_REG_POSITION(draw, 0x585);
ASSERT_REG_POSITION(index_array, 0x5F2);
ASSERT_REG_POSITION(query, 0x6C0);
ASSERT_REG_POSITION(vertex_array[0], 0x700);
ASSERT_REG_POSITION(blend, 0x780);
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
