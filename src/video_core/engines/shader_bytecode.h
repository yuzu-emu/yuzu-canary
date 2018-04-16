// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <string>
#include "common/bit_field.h"

namespace Tegra {
namespace Shader {

struct Register {
    Register() = default;

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

private:
    u64 value;
};

union Attribute {
    Attribute() = default;

    constexpr Attribute(u64 value) : value(value) {}

    enum class Index : u64 {
        Position = 7,
        Attribute_0 = 8,
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
    u64 value;
};

union Sampler {
    constexpr Sampler() = default;

    constexpr Sampler(u64 value) : value(value) {}

    enum class Index : u64 {
        Sampler_0 = 8,
    };

    BitField<36, 13, Index> index;
    u64 value;
};

union Uniform {
    BitField<20, 14, u64> offset;
    BitField<34, 5, u64> index;
};

union OpCode {
    enum class Id : u64 {
        TEXS = 0x6C,
        IPA = 0xE0,
        FFMA_IMM = 0x65,
        FFMA_CR = 0x93,
        FFMA_RC = 0xA3,
        FFMA_RR = 0xB3,

        FADD_C = 0x98B,
        FMUL_C = 0x98D,
        MUFU = 0xA10,
        FADD_R = 0xB8B,
        FMUL_R = 0xB8D,
        LD_A = 0x1DFB,
        ST_A = 0x1DFE,

        FSETP_R = 0x5BB,
        FSETP_C = 0x4BB,
        EXIT = 0xE30,
        KIL = 0xE33,

        FMUL_IMM = 0x70D,
        FMUL_IMM_x = 0x72D,
        FADD_IMM = 0x70B,
        FADD_IMM_x = 0x72B,
    };

    enum class Type {
        Trivial,
        Arithmetic,
        Ffma,
        Flow,
        Memory,
        Unknown,
    };

    struct Info {
        Type type;
        std::string name;
    };

    OpCode() = default;

    constexpr OpCode(Id value) : value(static_cast<u64>(value)) {}

    constexpr OpCode(u64 value) : value{value} {}

    constexpr Id EffectiveOpCode() const {
        switch (op1) {
        case Id::TEXS:
            return op1;
        }

        switch (op2) {
        case Id::IPA:
            return op2;
        }

        switch (op3) {
        case Id::FFMA_IMM:
        case Id::FFMA_CR:
        case Id::FFMA_RC:
        case Id::FFMA_RR:
            return op3;
        }

        switch (op4) {
        case Id::EXIT:
        case Id::FSETP_R:
        case Id::FSETP_C:
        case Id::KIL:
            return op4;
        }

        switch (op5) {
        case Id::MUFU:
        case Id::LD_A:
        case Id::ST_A:
        case Id::FADD_R:
        case Id::FADD_C:
        case Id::FMUL_R:
        case Id::FMUL_C:
            return op5;

        case Id::FMUL_IMM:
        case Id::FMUL_IMM_x:
            return Id::FMUL_IMM;

        case Id::FADD_IMM:
        case Id::FADD_IMM_x:
            return Id::FADD_IMM;
        }

        return static_cast<Id>(value);
    }

    static const Info& GetInfo(const OpCode& opcode) {
        static const std::map<Id, Info> info_table{BuildInfoTable()};
        const auto& search{info_table.find(opcode.EffectiveOpCode())};
        if (search != info_table.end()) {
            return search->second;
        }

        static const Info unknown{Type::Unknown, "UNK"};
        return unknown;
    }

    constexpr operator Id() const {
        return static_cast<Id>(value);
    }

    constexpr OpCode operator<<(size_t bits) const {
        return value << bits;
    }

    constexpr OpCode operator>>(size_t bits) const {
        return value >> bits;
    }

    template <typename T>
    constexpr u64 operator-(const T& oth) const {
        return value - oth;
    }

    constexpr u64 operator&(const OpCode& oth) const {
        return value & oth.value;
    }

    constexpr u64 operator~() const {
        return ~value;
    }

    static std::map<Id, Info> BuildInfoTable() {
        std::map<Id, Info> info_table;
        info_table[Id::TEXS] = {Type::Memory, "texs"};
        info_table[Id::LD_A] = {Type::Memory, "ld_a"};
        info_table[Id::ST_A] = {Type::Memory, "st_a"};
        info_table[Id::MUFU] = {Type::Arithmetic, "mufu"};
        info_table[Id::FFMA_IMM] = {Type::Ffma, "ffma_imm"};
        info_table[Id::FFMA_CR] = {Type::Ffma, "ffma_cr"};
        info_table[Id::FFMA_RC] = {Type::Ffma, "ffma_rc"};
        info_table[Id::FFMA_RR] = {Type::Ffma, "ffma_rr"};
        info_table[Id::FADD_R] = {Type::Arithmetic, "fadd_r"};
        info_table[Id::FADD_C] = {Type::Arithmetic, "fadd_c"};
        info_table[Id::FADD_IMM] = {Type::Arithmetic, "fadd_imm"};
        info_table[Id::FMUL_R] = {Type::Arithmetic, "fmul_r"};
        info_table[Id::FMUL_C] = {Type::Arithmetic, "fmul_c"};
        info_table[Id::FMUL_IMM] = {Type::Arithmetic, "fmul_imm"};
        info_table[Id::FSETP_C] = {Type::Arithmetic, "fsetp_c"};
        info_table[Id::FSETP_R] = {Type::Arithmetic, "fsetp_r"};
        info_table[Id::EXIT] = {Type::Trivial, "exit"};
        info_table[Id::IPA] = {Type::Trivial, "ipa"};
        info_table[Id::KIL] = {Type::Flow, "kil"};
        return info_table;
    }

    BitField<57, 7, Id> op1;
    BitField<56, 8, Id> op2;
    BitField<55, 9, Id> op3;
    BitField<52, 12, Id> op4;
    BitField<51, 13, Id> op5;
    u64 value;
};
static_assert(sizeof(OpCode) == 0x8, "Incorrect structure size");

} // namespace Shader
} // namespace Tegra

namespace std {

// TODO(bunne): The below is forbidden by the C++ standard, but works fine. See #330.
template <>
struct make_unsigned<Tegra::Shader::Attribute> {
    using type = Tegra::Shader::Attribute;
};

template <>
struct make_unsigned<Tegra::Shader::Register> {
    using type = Tegra::Shader::Register;
};

template <>
struct make_unsigned<Tegra::Shader::OpCode> {
    using type = Tegra::Shader::OpCode;
};

} // namespace std

namespace Tegra {
namespace Shader {

enum class Pred : u64 {
    UnusedIndex = 0x7,
    NeverExecute = 0xf,
};

enum class SubOp : u64 {
    Cos = 0x0,
    Sin = 0x1,
    Ex2 = 0x2,
    Lg2 = 0x3,
    Rcp = 0x4,
    Rsq = 0x5,
    Min = 0x8,
};

union Instruction {
    Instruction& operator=(const Instruction& instr) {
        hex = instr.hex;
        return *this;
    }

    OpCode opcode;
    BitField<0, 8, Register> gpr0;
    BitField<8, 8, Register> gpr8;
    BitField<16, 4, Pred> pred;
    BitField<20, 8, Register> gpr20;
    BitField<20, 7, SubOp> sub_op;
    BitField<28, 8, Register> gpr28;
    BitField<39, 8, Register> gpr39;

    union {
        BitField<20, 19, u64> imm20;
        BitField<45, 1, u64> negate_b;
        BitField<46, 1, u64> abs_a;
        BitField<48, 1, u64> negate_a;
        BitField<49, 1, u64> abs_b;
        BitField<50, 1, u64> abs_d;
        BitField<56, 1, u64> negate_imm;

        float GetImm20() const {
            u32 imm = imm20;
            imm <<= 12;
            imm |= negate_imm ? 0x80000000 : 0;
            return *(f32*)&imm;
        }
    } alu;

    union {
        BitField<48, 1, u64> negate_b;
        BitField<49, 1, u64> negate_c;
    } ffma;

    BitField<61, 1, u64> is_b_imm;
    BitField<60, 1, u64> is_b_gpr;
    BitField<59, 1, u64> is_c_gpr;

    Attribute attribute;
    Uniform uniform;
    Sampler sampler;

    u64 hex;
};
static_assert(sizeof(Instruction) == 0x8, "Incorrect structure size");
static_assert(std::is_standard_layout<Instruction>::value,
              "Structure does not have standard layout");

} // namespace Shader
} // namespace Tegra
