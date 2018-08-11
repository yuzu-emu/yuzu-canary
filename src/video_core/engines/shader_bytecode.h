// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <bitset>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "common/bit_field.h"
#include "common/common_types.h"

namespace Tegra::Shader {

struct Register {
    /// Number of registers
    static constexpr size_t NumRegisters = 256;

    /// Register 255 is special cased to always be 0
    static constexpr size_t ZeroIndex = 255;

    enum class Size : u64 {
        Byte = 0,
        Short = 1,
        Word = 2,
        Long = 3,
    };

    constexpr Register() = default;

    constexpr Register(u64 value) : value(value) {}

    constexpr operator u64() const {
        return value;
    }

    template <typename T>
    constexpr u64 operator-(const T& oth) const {
        return value - oth;
    }

    template <typename T>
    constexpr u64 operator&(const T& oth) const {
        return value & oth;
    }

    constexpr u64 operator&(const Register& oth) const {
        return value & oth.value;
    }

    constexpr u64 operator~() const {
        return ~value;
    }

    u64 GetSwizzledIndex(u64 elem) const {
        elem = (value + elem) & 3;
        return (value & ~3) + elem;
    }

private:
    u64 value{};
};

union Attribute {
    Attribute() = default;

    constexpr explicit Attribute(u64 value) : value(value) {}

    enum class Index : u64 {
        Position = 7,
        Attribute_0 = 8,
        Attribute_31 = 39,
        // This attribute contains a tuple of (~, ~, InstanceId, VertexId) when inside a vertex
        // shader, and a tuple of (TessCoord.x, TessCoord.y, TessCoord.z, ~) when inside a Tess Eval
        // shader.
        TessCoordInstanceIDVertexID = 47,
    };

    union {
        BitField<22, 2, u64> element;
        BitField<24, 6, Index> index;
        BitField<47, 3, u64> size;
    } fmt20;

    union {
        BitField<30, 2, u64> element;
        BitField<32, 6, Index> index;
    } fmt28;

    BitField<39, 8, u64> reg;
    u64 value{};
};

union Sampler {
    Sampler() = default;

    constexpr explicit Sampler(u64 value) : value(value) {}

    enum class Index : u64 {
        Sampler_0 = 8,
    };

    BitField<36, 13, Index> index;
    u64 value{};
};

} // namespace Tegra::Shader

namespace std {

// TODO(bunnei): The below is forbidden by the C++ standard, but works fine. See #330.
template <>
struct make_unsigned<Tegra::Shader::Attribute> {
    using type = Tegra::Shader::Attribute;
};

template <>
struct make_unsigned<Tegra::Shader::Register> {
    using type = Tegra::Shader::Register;
};

} // namespace std

namespace Tegra::Shader {

enum class Pred : u64 {
    UnusedIndex = 0x7,
    NeverExecute = 0xF,
};

enum class PredCondition : u64 {
    LessThan = 1,
    Equal = 2,
    LessEqual = 3,
    GreaterThan = 4,
    NotEqual = 5,
    GreaterEqual = 6,
    LessThanWithNan = 9,
    NotEqualWithNan = 13,
    // TODO(Subv): Other condition types
};

enum class PredOperation : u64 {
    And = 0,
    Or = 1,
    Xor = 2,
};

enum class LogicOperation : u64 {
    And = 0,
    Or = 1,
    Xor = 2,
    PassB = 3,
};

enum class SubOp : u64 {
    Cos = 0x0,
    Sin = 0x1,
    Ex2 = 0x2,
    Lg2 = 0x3,
    Rcp = 0x4,
    Rsq = 0x5,
    Sqrt = 0x8,
};

enum class F2iRoundingOp : u64 {
    None = 0,
    Floor = 1,
    Ceil = 2,
    Trunc = 3,
};

enum class F2fRoundingOp : u64 {
    None = 0,
    Pass = 3,
    Round = 8,
    Floor = 9,
    Ceil = 10,
    Trunc = 11,
};

enum class UniformType : u64 {
    UnsignedByte = 0,
    SignedByte = 1,
    UnsignedShort = 2,
    SignedShort = 3,
    Single = 4,
    Double = 5,
};

enum class IMinMaxExchange : u64 {
    None = 0,
    XLo = 1,
    XMed = 2,
    XHi = 3,
};

enum class FlowCondition : u64 {
    Always = 0xF,
    Fcsm_Tr = 0x1C, // TODO(bunnei): What is this used for?
};

union Instruction {
    Instruction& operator=(const Instruction& instr) {
        value = instr.value;
        return *this;
    }

    constexpr Instruction(u64 value) : value{value} {}

    BitField<0, 8, Register> gpr0;
    BitField<8, 8, Register> gpr8;
    union {
        BitField<16, 4, Pred> full_pred;
        BitField<16, 3, u64> pred_index;
    } pred;
    BitField<19, 1, u64> negate_pred;
    BitField<20, 8, Register> gpr20;
    BitField<20, 4, SubOp> sub_op;
    BitField<28, 8, Register> gpr28;
    BitField<39, 8, Register> gpr39;
    BitField<48, 16, u64> opcode;

    union {
        BitField<20, 19, u64> imm20_19;
        BitField<20, 32, s64> imm20_32;
        BitField<45, 1, u64> negate_b;
        BitField<46, 1, u64> abs_a;
        BitField<48, 1, u64> negate_a;
        BitField<49, 1, u64> abs_b;
        BitField<50, 1, u64> saturate_d;
        BitField<56, 1, u64> negate_imm;

        union {
            BitField<39, 3, u64> pred;
            BitField<42, 1, u64> negate_pred;
        } fmnmx;

        union {
            BitField<39, 1, u64> invert_a;
            BitField<40, 1, u64> invert_b;
            BitField<41, 2, LogicOperation> operation;
            BitField<44, 2, u64> unk44;
            BitField<48, 3, Pred> pred48;
        } lop;

        union {
            BitField<53, 2, LogicOperation> operation;
            BitField<55, 1, u64> invert_a;
            BitField<56, 1, u64> invert_b;
        } lop32i;

        u32 GetImm20_19() const {
            u32 imm{static_cast<u32>(imm20_19)};
            imm <<= 12;
            imm |= negate_imm ? 0x80000000 : 0;
            return imm;
        }

        u32 GetImm20_32() const {
            return static_cast<u32>(imm20_32);
        }

        s32 GetSignedImm20_20() const {
            u32 immediate = static_cast<u32>(imm20_19 | (negate_imm << 19));
            // Sign extend the 20-bit value.
            u32 mask = 1U << (20 - 1);
            return static_cast<s32>((immediate ^ mask) - mask);
        }
    } alu;

    union {
        BitField<48, 1, u64> is_signed;
    } shift;

    union {
        BitField<39, 5, u64> shift_amount;
        BitField<48, 1, u64> negate_b;
        BitField<49, 1, u64> negate_a;
    } alu_integer;

    union {
        BitField<39, 3, u64> pred;
        BitField<42, 1, u64> neg_pred;
    } sel;

    union {
        BitField<39, 3, u64> pred;
        BitField<42, 1, u64> negate_pred;
        BitField<43, 2, IMinMaxExchange> exchange;
        BitField<48, 1, u64> is_signed;
    } imnmx;

    union {
        BitField<54, 1, u64> saturate;
        BitField<56, 1, u64> negate_a;
    } iadd32i;

    union {
        BitField<53, 1, u64> negate_b;
        BitField<54, 1, u64> abs_a;
        BitField<56, 1, u64> negate_a;
        BitField<57, 1, u64> abs_b;
    } fadd32i;

    union {
        BitField<20, 8, u64> shift_position;
        BitField<28, 8, u64> shift_length;
        BitField<48, 1, u64> negate_b;
        BitField<49, 1, u64> negate_a;

        u64 GetLeftShiftValue() const {
            return 32 - (shift_position + shift_length);
        }
    } bfe;

    union {
        BitField<0, 5, FlowCondition> cond;
    } flow;

    union {
        BitField<48, 1, u64> negate_b;
        BitField<49, 1, u64> negate_c;
    } ffma;

    union {
        BitField<48, 3, UniformType> type;
        BitField<44, 2, u64> unknown;
    } ld_c;

    union {
        BitField<0, 3, u64> pred0;
        BitField<3, 3, u64> pred3;
        BitField<7, 1, u64> abs_a;
        BitField<39, 3, u64> pred39;
        BitField<42, 1, u64> neg_pred;
        BitField<43, 1, u64> neg_a;
        BitField<44, 1, u64> abs_b;
        BitField<45, 2, PredOperation> op;
        BitField<47, 1, u64> ftz;
        BitField<48, 4, PredCondition> cond;
        BitField<56, 1, u64> neg_b;
    } fsetp;

    union {
        BitField<0, 3, u64> pred0;
        BitField<3, 3, u64> pred3;
        BitField<39, 3, u64> pred39;
        BitField<42, 1, u64> neg_pred;
        BitField<45, 2, PredOperation> op;
        BitField<48, 1, u64> is_signed;
        BitField<49, 3, PredCondition> cond;
    } isetp;

    union {
        BitField<0, 3, u64> pred0;
        BitField<3, 3, u64> pred3;
        BitField<12, 3, u64> pred12;
        BitField<15, 1, u64> neg_pred12;
        BitField<24, 2, PredOperation> cond;
        BitField<29, 3, u64> pred29;
        BitField<32, 1, u64> neg_pred29;
        BitField<39, 3, u64> pred39;
        BitField<42, 1, u64> neg_pred39;
        BitField<45, 2, PredOperation> op;
    } psetp;

    union {
        BitField<39, 3, u64> pred39;
        BitField<42, 1, u64> neg_pred;
        BitField<43, 1, u64> neg_a;
        BitField<44, 1, u64> abs_b;
        BitField<45, 2, PredOperation> op;
        BitField<48, 4, PredCondition> cond;
        BitField<52, 1, u64> bf;
        BitField<53, 1, u64> neg_b;
        BitField<54, 1, u64> abs_a;
        BitField<55, 1, u64> ftz;
        BitField<56, 1, u64> neg_imm;
    } fset;

    union {
        BitField<39, 3, u64> pred39;
        BitField<42, 1, u64> neg_pred;
        BitField<44, 1, u64> bf;
        BitField<45, 2, PredOperation> op;
        BitField<48, 1, u64> is_signed;
        BitField<49, 3, PredCondition> cond;
    } iset;

    union {
        BitField<8, 2, Register::Size> dest_size;
        BitField<10, 2, Register::Size> src_size;
        BitField<12, 1, u64> is_output_signed;
        BitField<13, 1, u64> is_input_signed;
        BitField<41, 2, u64> selector;
        BitField<45, 1, u64> negate_a;
        BitField<49, 1, u64> abs_a;

        union {
            BitField<39, 2, F2iRoundingOp> rounding;
        } f2i;

        union {
            BitField<39, 4, F2fRoundingOp> rounding;
        } f2f;
    } conversion;

    union {
        BitField<31, 4, u64> component_mask;

        bool IsComponentEnabled(size_t component) const {
            return ((1ull << component) & component_mask) != 0;
        }
    } tex;

    union {
        BitField<50, 3, u64> component_mask_selector;
        BitField<0, 8, Register> gpr0;
        BitField<28, 8, Register> gpr28;

        bool HasTwoDestinations() const {
            return gpr28.Value() != Register::ZeroIndex;
        }

        bool IsComponentEnabled(size_t component) const {
            static constexpr std::array<std::array<u32, 8>, 4> mask_lut{
                {{},
                 {0x1, 0x2, 0x4, 0x8, 0x3},
                 {0x1, 0x2, 0x4, 0x8, 0x3, 0x9, 0xa, 0xc},
                 {0x7, 0xb, 0xd, 0xe, 0xf}}};

            size_t index{gpr0.Value() != Register::ZeroIndex ? 1U : 0U};
            index |= gpr28.Value() != Register::ZeroIndex ? 2 : 0;

            return ((1ull << component) & mask_lut[index][component_mask_selector]) != 0;
        }
    } texs;

    union {
        BitField<20, 24, u64> target;
        BitField<5, 1, u64> constant_buffer;

        s32 GetBranchTarget() const {
            // Sign extend the branch target offset
            u32 mask = 1U << (24 - 1);
            u32 value = static_cast<u32>(target);
            // The branch offset is relative to the next instruction and is stored in bytes, so
            // divide it by the size of an instruction and add 1 to it.
            return static_cast<s32>((value ^ mask) - mask) / sizeof(Instruction) + 1;
        }
    } bra;

    union {
        BitField<20, 14, u64> offset;
        BitField<34, 5, u64> index;
    } cbuf34;

    union {
        BitField<20, 16, s64> offset;
        BitField<36, 5, u64> index;
    } cbuf36;

    BitField<61, 1, u64> is_b_imm;
    BitField<60, 1, u64> is_b_gpr;
    BitField<59, 1, u64> is_c_gpr;

    Attribute attribute;
    Sampler sampler;

    u64 value;
};
static_assert(sizeof(Instruction) == 0x8, "Incorrect structure size");
static_assert(std::is_standard_layout_v<Instruction>, "Instruction is not standard layout");

class OpCode {
public:
    enum class Id {
        KIL,
        SSY,
        SYNC,
        DEPBAR,
        BFE_C,
        BFE_R,
        BFE_IMM,
        BRA,
        LD_A,
        LD_C,
        ST_A,
        TEX,
        TEXQ, // Texture Query
        TEXS, // Texture Fetch with scalar/non-vec4 source/destinations
        TLDS, // Texture Load with scalar/non-vec4 source/destinations
        EXIT,
        IPA,
        FFMA_IMM, // Fused Multiply and Add
        FFMA_CR,
        FFMA_RC,
        FFMA_RR,
        FADD_C,
        FADD_R,
        FADD_IMM,
        FADD32I,
        FMUL_C,
        FMUL_R,
        FMUL_IMM,
        FMUL32_IMM,
        IADD_C,
        IADD_R,
        IADD_IMM,
        IADD32I,
        ISCADD_C, // Scale and Add
        ISCADD_R,
        ISCADD_IMM,
        SEL_C,
        SEL_R,
        SEL_IMM,
        MUFU,  // Multi-Function Operator
        RRO_C, // Range Reduction Operator
        RRO_R,
        RRO_IMM,
        F2F_C,
        F2F_R,
        F2F_IMM,
        F2I_C,
        F2I_R,
        F2I_IMM,
        I2F_C,
        I2F_R,
        I2F_IMM,
        I2I_C,
        I2I_R,
        I2I_IMM,
        LOP_C,
        LOP_R,
        LOP_IMM,
        LOP32I,
        MOV_C,
        MOV_R,
        MOV_IMM,
        MOV32_IMM,
        SHL_C,
        SHL_R,
        SHL_IMM,
        SHR_C,
        SHR_R,
        SHR_IMM,
        FMNMX_C,
        FMNMX_R,
        FMNMX_IMM,
        IMNMX_C,
        IMNMX_R,
        IMNMX_IMM,
        FSETP_C, // Set Predicate
        FSETP_R,
        FSETP_IMM,
        FSET_C,
        FSET_R,
        FSET_IMM,
        ISETP_C,
        ISETP_IMM,
        ISETP_R,
        ISET_R,
        ISET_C,
        ISET_IMM,
        PSETP,
        XMAD_IMM,
        XMAD_CR,
        XMAD_RC,
        XMAD_RR,
    };

    enum class Type {
        Trivial,
        Arithmetic,
        ArithmeticImmediate,
        ArithmeticInteger,
        ArithmeticIntegerImmediate,
        Bfe,
        Shift,
        Ffma,
        Flow,
        Synch,
        Memory,
        FloatSet,
        FloatSetPredicate,
        IntegerSet,
        IntegerSetPredicate,
        PredicateSetPredicate,
        Conversion,
        Unknown,
    };

    /// Returns whether an opcode has an execution predicate field or not (ie, whether it can be
    /// conditionally executed).
    static bool IsPredicatedInstruction(Id opcode) {
        // TODO(Subv): Add the rest of unpredicated instructions.
        return opcode != Id::SSY;
    }

    class Matcher {
    public:
        Matcher(const char* const name, u16 mask, u16 expected, OpCode::Id id, OpCode::Type type)
            : name{name}, mask{mask}, expected{expected}, id{id}, type{type} {}

        const char* GetName() const {
            return name;
        }

        u16 GetMask() const {
            return mask;
        }

        Id GetId() const {
            return id;
        }

        Type GetType() const {
            return type;
        }

        /**
         * Tests to see if the given instruction is the instruction this matcher represents.
         * @param instruction The instruction to test
         * @returns true if the given instruction matches.
         */
        bool Matches(u16 instruction) const {
            return (instruction & mask) == expected;
        }

    private:
        const char* name;
        u16 mask;
        u16 expected;
        Id id;
        Type type;
    };

    static boost::optional<const Matcher&> Decode(Instruction instr) {
        static const auto table{GetDecodeTable()};

        const auto matches_instruction = [instr](const auto& matcher) {
            return matcher.Matches(static_cast<u16>(instr.opcode));
        };

        auto iter = std::find_if(table.begin(), table.end(), matches_instruction);
        return iter != table.end() ? boost::optional<const Matcher&>(*iter) : boost::none;
    }

private:
    struct Detail {
    private:
        static constexpr size_t opcode_bitsize = 16;

        /**
         * Generates the mask and the expected value after masking from a given bitstring.
         * A '0' in a bitstring indicates that a zero must be present at that bit position.
         * A '1' in a bitstring indicates that a one must be present at that bit position.
         */
        static auto GetMaskAndExpect(const char* const bitstring) {
            u16 mask = 0, expect = 0;
            for (size_t i = 0; i < opcode_bitsize; i++) {
                const size_t bit_position = opcode_bitsize - i - 1;
                switch (bitstring[i]) {
                case '0':
                    mask |= 1 << bit_position;
                    break;
                case '1':
                    expect |= 1 << bit_position;
                    mask |= 1 << bit_position;
                    break;
                default:
                    // Ignore
                    break;
                }
            }
            return std::make_tuple(mask, expect);
        }

    public:
        /// Creates a matcher that can match and parse instructions based on bitstring.
        static auto GetMatcher(const char* const bitstring, OpCode::Id op, OpCode::Type type,
                               const char* const name) {
            const auto mask_expect = GetMaskAndExpect(bitstring);
            return Matcher(name, std::get<0>(mask_expect), std::get<1>(mask_expect), op, type);
        }
    };

    static std::vector<Matcher> GetDecodeTable() {
        std::vector<Matcher> table = {
#define INST(bitstring, op, type, name) Detail::GetMatcher(bitstring, op, type, name)
            INST("111000110011----", Id::KIL, Type::Flow, "KIL"),
            INST("111000101001----", Id::SSY, Type::Flow, "SSY"),
            INST("111000100100----", Id::BRA, Type::Flow, "BRA"),
            INST("1111000011110---", Id::DEPBAR, Type::Synch, "DEPBAR"),
            INST("1111000011111---", Id::SYNC, Type::Synch, "SYNC"),
            INST("1110111111011---", Id::LD_A, Type::Memory, "LD_A"),
            INST("1110111110010---", Id::LD_C, Type::Memory, "LD_C"),
            INST("1110111111110---", Id::ST_A, Type::Memory, "ST_A"),
            INST("110000----111---", Id::TEX, Type::Memory, "TEX"),
            INST("1101111101001---", Id::TEXQ, Type::Memory, "TEXQ"),
            INST("1101100---------", Id::TEXS, Type::Memory, "TEXS"),
            INST("1101101---------", Id::TLDS, Type::Memory, "TLDS"),
            INST("111000110000----", Id::EXIT, Type::Trivial, "EXIT"),
            INST("11100000--------", Id::IPA, Type::Trivial, "IPA"),
            INST("0011001-1-------", Id::FFMA_IMM, Type::Ffma, "FFMA_IMM"),
            INST("010010011-------", Id::FFMA_CR, Type::Ffma, "FFMA_CR"),
            INST("010100011-------", Id::FFMA_RC, Type::Ffma, "FFMA_RC"),
            INST("010110011-------", Id::FFMA_RR, Type::Ffma, "FFMA_RR"),
            INST("0100110001011---", Id::FADD_C, Type::Arithmetic, "FADD_C"),
            INST("0101110001011---", Id::FADD_R, Type::Arithmetic, "FADD_R"),
            INST("0011100-01011---", Id::FADD_IMM, Type::Arithmetic, "FADD_IMM"),
            INST("000010----------", Id::FADD32I, Type::ArithmeticImmediate, "FADD32I"),
            INST("0100110001101---", Id::FMUL_C, Type::Arithmetic, "FMUL_C"),
            INST("0101110001101---", Id::FMUL_R, Type::Arithmetic, "FMUL_R"),
            INST("0011100-01101---", Id::FMUL_IMM, Type::Arithmetic, "FMUL_IMM"),
            INST("00011110--------", Id::FMUL32_IMM, Type::ArithmeticImmediate, "FMUL32_IMM"),
            INST("0100110000010---", Id::IADD_C, Type::ArithmeticInteger, "IADD_C"),
            INST("0101110000010---", Id::IADD_R, Type::ArithmeticInteger, "IADD_R"),
            INST("0011100-00010---", Id::IADD_IMM, Type::ArithmeticInteger, "IADD_IMM"),
            INST("0001110---------", Id::IADD32I, Type::ArithmeticIntegerImmediate, "IADD32I"),
            INST("0100110000011---", Id::ISCADD_C, Type::ArithmeticInteger, "ISCADD_C"),
            INST("0101110000011---", Id::ISCADD_R, Type::ArithmeticInteger, "ISCADD_R"),
            INST("0011100-00011---", Id::ISCADD_IMM, Type::ArithmeticInteger, "ISCADD_IMM"),
            INST("0100110010100---", Id::SEL_C, Type::ArithmeticInteger, "SEL_C"),
            INST("0101110010100---", Id::SEL_R, Type::ArithmeticInteger, "SEL_R"),
            INST("0011100010100---", Id::SEL_IMM, Type::ArithmeticInteger, "SEL_IMM"),
            INST("0101000010000---", Id::MUFU, Type::Arithmetic, "MUFU"),
            INST("0100110010010---", Id::RRO_C, Type::Arithmetic, "RRO_C"),
            INST("0101110010010---", Id::RRO_R, Type::Arithmetic, "RRO_R"),
            INST("0011100-10010---", Id::RRO_IMM, Type::Arithmetic, "RRO_IMM"),
            INST("0100110010101---", Id::F2F_C, Type::Conversion, "F2F_C"),
            INST("0101110010101---", Id::F2F_R, Type::Conversion, "F2F_R"),
            INST("0011100-10101---", Id::F2F_IMM, Type::Conversion, "F2F_IMM"),
            INST("0100110010110---", Id::F2I_C, Type::Conversion, "F2I_C"),
            INST("0101110010110---", Id::F2I_R, Type::Conversion, "F2I_R"),
            INST("0011100-10110---", Id::F2I_IMM, Type::Conversion, "F2I_IMM"),
            INST("0100110010011---", Id::MOV_C, Type::Arithmetic, "MOV_C"),
            INST("0101110010011---", Id::MOV_R, Type::Arithmetic, "MOV_R"),
            INST("0011100-10011---", Id::MOV_IMM, Type::Arithmetic, "MOV_IMM"),
            INST("000000010000----", Id::MOV32_IMM, Type::ArithmeticImmediate, "MOV32_IMM"),
            INST("0100110001100---", Id::FMNMX_C, Type::Arithmetic, "FMNMX_C"),
            INST("0101110001100---", Id::FMNMX_R, Type::Arithmetic, "FMNMX_R"),
            INST("0011100-01100---", Id::FMNMX_IMM, Type::Arithmetic, "FMNMX_IMM"),
            INST("0100110000100---", Id::IMNMX_C, Type::ArithmeticInteger, "IMNMX_C"),
            INST("0101110000100---", Id::IMNMX_R, Type::ArithmeticInteger, "IMNMX_R"),
            INST("0011100-00100---", Id::IMNMX_IMM, Type::ArithmeticInteger, "IMNMX_IMM"),
            INST("0100110000000---", Id::BFE_C, Type::Bfe, "BFE_C"),
            INST("0101110000000---", Id::BFE_R, Type::Bfe, "BFE_R"),
            INST("0011100-00000---", Id::BFE_IMM, Type::Bfe, "BFE_IMM"),
            INST("0100110001000---", Id::LOP_C, Type::ArithmeticInteger, "LOP_C"),
            INST("0101110001000---", Id::LOP_R, Type::ArithmeticInteger, "LOP_R"),
            INST("0011100001000---", Id::LOP_IMM, Type::ArithmeticInteger, "LOP_IMM"),
            INST("000001----------", Id::LOP32I, Type::ArithmeticIntegerImmediate, "LOP32I"),
            INST("0100110001001---", Id::SHL_C, Type::Shift, "SHL_C"),
            INST("0101110001001---", Id::SHL_R, Type::Shift, "SHL_R"),
            INST("0011100-01001---", Id::SHL_IMM, Type::Shift, "SHL_IMM"),
            INST("0100110000101---", Id::SHR_C, Type::Shift, "SHR_C"),
            INST("0101110000101---", Id::SHR_R, Type::Shift, "SHR_R"),
            INST("0011100-00101---", Id::SHR_IMM, Type::Shift, "SHR_IMM"),
            INST("0100110011100---", Id::I2I_C, Type::Conversion, "I2I_C"),
            INST("0101110011100---", Id::I2I_R, Type::Conversion, "I2I_R"),
            INST("01110001-1000---", Id::I2I_IMM, Type::Conversion, "I2I_IMM"),
            INST("0100110010111---", Id::I2F_C, Type::Conversion, "I2F_C"),
            INST("0101110010111---", Id::I2F_R, Type::Conversion, "I2F_R"),
            INST("0011100-10111---", Id::I2F_IMM, Type::Conversion, "I2F_IMM"),
            INST("01011000--------", Id::FSET_R, Type::FloatSet, "FSET_R"),
            INST("0100100---------", Id::FSET_C, Type::FloatSet, "FSET_C"),
            INST("0011000---------", Id::FSET_IMM, Type::FloatSet, "FSET_IMM"),
            INST("010010111011----", Id::FSETP_C, Type::FloatSetPredicate, "FSETP_C"),
            INST("010110111011----", Id::FSETP_R, Type::FloatSetPredicate, "FSETP_R"),
            INST("0011011-1011----", Id::FSETP_IMM, Type::FloatSetPredicate, "FSETP_IMM"),
            INST("010010110110----", Id::ISETP_C, Type::IntegerSetPredicate, "ISETP_C"),
            INST("010110110110----", Id::ISETP_R, Type::IntegerSetPredicate, "ISETP_R"),
            INST("0011011-0110----", Id::ISETP_IMM, Type::IntegerSetPredicate, "ISETP_IMM"),
            INST("010110110101----", Id::ISET_R, Type::IntegerSet, "ISET_R"),
            INST("010010110101----", Id::ISET_C, Type::IntegerSet, "ISET_C"),
            INST("0011011-0101----", Id::ISET_IMM, Type::IntegerSet, "ISET_IMM"),
            INST("0101000010010---", Id::PSETP, Type::PredicateSetPredicate, "PSETP"),
            INST("0011011-00------", Id::XMAD_IMM, Type::Arithmetic, "XMAD_IMM"),
            INST("0100111---------", Id::XMAD_CR, Type::Arithmetic, "XMAD_CR"),
            INST("010100010-------", Id::XMAD_RC, Type::Arithmetic, "XMAD_RC"),
            INST("0101101100------", Id::XMAD_RR, Type::Arithmetic, "XMAD_RR"),
        };
#undef INST
        std::stable_sort(table.begin(), table.end(), [](const auto& a, const auto& b) {
            // If a matcher has more bits in its mask it is more specific, so it
            // should come first.
            return std::bitset<16>(a.GetMask()).count() > std::bitset<16>(b.GetMask()).count();
        });

        return table;
    }
};

} // namespace Tegra::Shader
