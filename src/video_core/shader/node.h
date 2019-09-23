// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "common/common_types.h"
#include "video_core/engines/shader_bytecode.h"

namespace VideoCommon::Shader {

enum class OperationCode {
    Assign, /// (float& dest, float src) -> void

    Select, /// (MetaArithmetic, bool pred, float a, float b) -> float

    FAdd,          /// (MetaArithmetic, float a, float b) -> float
    FMul,          /// (MetaArithmetic, float a, float b) -> float
    FDiv,          /// (MetaArithmetic, float a, float b) -> float
    FFma,          /// (MetaArithmetic, float a, float b, float c) -> float
    FNegate,       /// (MetaArithmetic, float a) -> float
    FAbsolute,     /// (MetaArithmetic, float a) -> float
    FClamp,        /// (MetaArithmetic, float value, float min, float max) -> float
    FCastHalf0,    /// (MetaArithmetic, f16vec2 a) -> float
    FCastHalf1,    /// (MetaArithmetic, f16vec2 a) -> float
    FMin,          /// (MetaArithmetic, float a, float b) -> float
    FMax,          /// (MetaArithmetic, float a, float b) -> float
    FCos,          /// (MetaArithmetic, float a) -> float
    FSin,          /// (MetaArithmetic, float a) -> float
    FExp2,         /// (MetaArithmetic, float a) -> float
    FLog2,         /// (MetaArithmetic, float a) -> float
    FInverseSqrt,  /// (MetaArithmetic, float a) -> float
    FSqrt,         /// (MetaArithmetic, float a) -> float
    FRoundEven,    /// (MetaArithmetic, float a) -> float
    FFloor,        /// (MetaArithmetic, float a) -> float
    FCeil,         /// (MetaArithmetic, float a) -> float
    FTrunc,        /// (MetaArithmetic, float a) -> float
    FCastInteger,  /// (MetaArithmetic, int a) -> float
    FCastUInteger, /// (MetaArithmetic, uint a) -> float

    IAdd,                  /// (MetaArithmetic, int a, int b) -> int
    IMul,                  /// (MetaArithmetic, int a, int b) -> int
    IDiv,                  /// (MetaArithmetic, int a, int b) -> int
    INegate,               /// (MetaArithmetic, int a) -> int
    IAbsolute,             /// (MetaArithmetic, int a) -> int
    IMin,                  /// (MetaArithmetic, int a, int b) -> int
    IMax,                  /// (MetaArithmetic, int a, int b) -> int
    ICastFloat,            /// (MetaArithmetic, float a) -> int
    ICastUnsigned,         /// (MetaArithmetic, uint a) -> int
    ILogicalShiftLeft,     /// (MetaArithmetic, int a, uint b) -> int
    ILogicalShiftRight,    /// (MetaArithmetic, int a, uint b) -> int
    IArithmeticShiftRight, /// (MetaArithmetic, int a, uint b) -> int
    IBitwiseAnd,           /// (MetaArithmetic, int a, int b) -> int
    IBitwiseOr,            /// (MetaArithmetic, int a, int b) -> int
    IBitwiseXor,           /// (MetaArithmetic, int a, int b) -> int
    IBitwiseNot,           /// (MetaArithmetic, int a) -> int
    IBitfieldInsert,       /// (MetaArithmetic, int base, int insert, int offset, int bits) -> int
    IBitfieldExtract,      /// (MetaArithmetic, int value, int offset, int offset) -> int
    IBitCount,             /// (MetaArithmetic, int) -> int

    UAdd,                  /// (MetaArithmetic, uint a, uint b) -> uint
    UMul,                  /// (MetaArithmetic, uint a, uint b) -> uint
    UDiv,                  /// (MetaArithmetic, uint a, uint b) -> uint
    UMin,                  /// (MetaArithmetic, uint a, uint b) -> uint
    UMax,                  /// (MetaArithmetic, uint a, uint b) -> uint
    UCastFloat,            /// (MetaArithmetic, float a) -> uint
    UCastSigned,           /// (MetaArithmetic, int a) -> uint
    ULogicalShiftLeft,     /// (MetaArithmetic, uint a, uint b) -> uint
    ULogicalShiftRight,    /// (MetaArithmetic, uint a, uint b) -> uint
    UArithmeticShiftRight, /// (MetaArithmetic, uint a, uint b) -> uint
    UBitwiseAnd,           /// (MetaArithmetic, uint a, uint b) -> uint
    UBitwiseOr,            /// (MetaArithmetic, uint a, uint b) -> uint
    UBitwiseXor,           /// (MetaArithmetic, uint a, uint b) -> uint
    UBitwiseNot,           /// (MetaArithmetic, uint a) -> uint
    UBitfieldInsert,  /// (MetaArithmetic, uint base, uint insert, int offset, int bits) -> uint
    UBitfieldExtract, /// (MetaArithmetic, uint value, int offset, int offset) -> uint
    UBitCount,        /// (MetaArithmetic, uint) -> uint

    HAdd,       /// (MetaArithmetic, f16vec2 a, f16vec2 b) -> f16vec2
    HMul,       /// (MetaArithmetic, f16vec2 a, f16vec2 b) -> f16vec2
    HFma,       /// (MetaArithmetic, f16vec2 a, f16vec2 b, f16vec2 c) -> f16vec2
    HAbsolute,  /// (f16vec2 a) -> f16vec2
    HNegate,    /// (f16vec2 a, bool first, bool second) -> f16vec2
    HClamp,     /// (f16vec2 src, float min, float max) -> f16vec2
    HCastFloat, /// (MetaArithmetic, float a) -> f16vec2
    HUnpack,    /// (Tegra::Shader::HalfType, T value) -> f16vec2
    HMergeF32,  /// (f16vec2 src) -> float
    HMergeH0,   /// (f16vec2 dest, f16vec2 src) -> f16vec2
    HMergeH1,   /// (f16vec2 dest, f16vec2 src) -> f16vec2
    HPack2,     /// (float a, float b) -> f16vec2

    LogicalAssign, /// (bool& dst, bool src) -> void
    LogicalAnd,    /// (bool a, bool b) -> bool
    LogicalOr,     /// (bool a, bool b) -> bool
    LogicalXor,    /// (bool a, bool b) -> bool
    LogicalNegate, /// (bool a) -> bool
    LogicalPick2,  /// (bool2 pair, uint index) -> bool
    LogicalAnd2,   /// (bool2 a) -> bool

    LogicalFLessThan,     /// (float a, float b) -> bool
    LogicalFEqual,        /// (float a, float b) -> bool
    LogicalFLessEqual,    /// (float a, float b) -> bool
    LogicalFGreaterThan,  /// (float a, float b) -> bool
    LogicalFNotEqual,     /// (float a, float b) -> bool
    LogicalFGreaterEqual, /// (float a, float b) -> bool
    LogicalFIsNan,        /// (float a) -> bool

    LogicalILessThan,     /// (int a, int b) -> bool
    LogicalIEqual,        /// (int a, int b) -> bool
    LogicalILessEqual,    /// (int a, int b) -> bool
    LogicalIGreaterThan,  /// (int a, int b) -> bool
    LogicalINotEqual,     /// (int a, int b) -> bool
    LogicalIGreaterEqual, /// (int a, int b) -> bool

    LogicalULessThan,     /// (uint a, uint b) -> bool
    LogicalUEqual,        /// (uint a, uint b) -> bool
    LogicalULessEqual,    /// (uint a, uint b) -> bool
    LogicalUGreaterThan,  /// (uint a, uint b) -> bool
    LogicalUNotEqual,     /// (uint a, uint b) -> bool
    LogicalUGreaterEqual, /// (uint a, uint b) -> bool

    Logical2HLessThan,            /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HEqual,               /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HLessEqual,           /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HGreaterThan,         /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HNotEqual,            /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HGreaterEqual,        /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HLessThanWithNan,     /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HEqualWithNan,        /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HLessEqualWithNan,    /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HGreaterThanWithNan,  /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HNotEqualWithNan,     /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2
    Logical2HGreaterEqualWithNan, /// (MetaHalfArithmetic, f16vec2 a, f16vec2) -> bool2

    Texture,                /// (MetaTexture, float[N] coords) -> float4
    TextureLod,             /// (MetaTexture, float[N] coords) -> float4
    TextureGather,          /// (MetaTexture, float[N] coords) -> float4
    TextureQueryDimensions, /// (MetaTexture, float a) -> float4
    TextureQueryLod,        /// (MetaTexture, float[N] coords) -> float4
    TexelFetch,             /// (MetaTexture, int[N], int) -> float4

    ImageStore,          /// (MetaImage, int[N] values) -> void
    AtomicImageAdd,      /// (MetaImage, int[N] coords) -> void
    AtomicImageMin,      /// (MetaImage, int[N] coords) -> void
    AtomicImageMax,      /// (MetaImage, int[N] coords) -> void
    AtomicImageAnd,      /// (MetaImage, int[N] coords) -> void
    AtomicImageOr,       /// (MetaImage, int[N] coords) -> void
    AtomicImageXor,      /// (MetaImage, int[N] coords) -> void
    AtomicImageExchange, /// (MetaImage, int[N] coords) -> void

    Branch,         /// (uint branch_target) -> void
    BranchIndirect, /// (uint branch_target) -> void
    PushFlowStack,  /// (uint branch_target) -> void
    PopFlowStack,   /// () -> void
    Exit,           /// () -> void
    Discard,        /// () -> void

    EmitVertex,   /// () -> void
    EndPrimitive, /// () -> void

    YNegate,            /// () -> float
    LocalInvocationIdX, /// () -> uint
    LocalInvocationIdY, /// () -> uint
    LocalInvocationIdZ, /// () -> uint
    WorkGroupIdX,       /// () -> uint
    WorkGroupIdY,       /// () -> uint
    WorkGroupIdZ,       /// () -> uint

    BallotThread, /// (bool) -> uint
    VoteAll,      /// (bool) -> bool
    VoteAny,      /// (bool) -> bool
    VoteEqual,    /// (bool) -> bool

    ShuffleIndexed,   /// (uint value, uint index, uint width) -> uint
    ShuffleUp,        /// (uint value, uint index, uint width) -> uint
    ShuffleDown,      /// (uint value, uint index, uint width) -> uint
    ShuffleButterfly, /// (uint value, uint index, uint width) -> uint

    InRangeShuffleIndexed,   /// (uint index, uint width) -> bool
    InRangeShuffleUp,        /// (uint index, uint width) -> bool
    InRangeShuffleDown,      /// (uint index, uint width) -> bool
    InRangeShuffleButterfly, /// (uint index, uint width) -> bool

    Amount,
};

enum class InternalFlag {
    Zero = 0,
    Sign = 1,
    Carry = 2,
    Overflow = 3,
    Amount = 4,
};

enum class MetaStackClass {
    Ssy,
    Pbk,
};

class OperationNode;
class ConditionalNode;
class GprNode;
class ImmediateNode;
class InternalFlagNode;
class PredicateNode;
class AbufNode;
class CbufNode;
class LmemNode;
class SmemNode;
class GmemNode;
class CommentNode;

using NodeData =
    std::variant<OperationNode, ConditionalNode, GprNode, ImmediateNode, InternalFlagNode,
                 PredicateNode, AbufNode, CbufNode, LmemNode, SmemNode, GmemNode, CommentNode>;
using Node = std::shared_ptr<NodeData>;
using Node4 = std::array<Node, 4>;
using NodeBlock = std::vector<Node>;

class Sampler {
public:
    /// This constructor is for bound samplers
    explicit Sampler(std::size_t offset, std::size_t index, Tegra::Shader::TextureType type,
                     bool is_array, bool is_shadow)
        : offset{offset}, index{index}, type{type}, is_array{is_array}, is_shadow{is_shadow},
          is_bindless{false} {}

    /// This constructor is for bindless samplers
    explicit Sampler(u32 cbuf_index, u32 cbuf_offset, std::size_t index,
                     Tegra::Shader::TextureType type, bool is_array, bool is_shadow)
        : offset{(static_cast<u64>(cbuf_index) << 32) | cbuf_offset}, index{index}, type{type},
          is_array{is_array}, is_shadow{is_shadow}, is_bindless{true} {}

    /// This constructor is for serialization/deserialization
    explicit Sampler(std::size_t offset, std::size_t index, Tegra::Shader::TextureType type,
                     bool is_array, bool is_shadow, bool is_bindless)
        : offset{offset}, index{index}, type{type}, is_array{is_array}, is_shadow{is_shadow},
          is_bindless{is_bindless} {}

    void SetType(Tegra::Shader::TextureType type_, bool is_array_, bool is_shadow_) {
        type = type_;
        is_array = is_array_;
        is_shadow = is_shadow_;
    }

    std::size_t GetOffset() const {
        return offset;
    }

    std::size_t GetIndex() const {
        return index;
    }

    Tegra::Shader::TextureType GetType() const {
        return type;
    }

    bool IsArray() const {
        return is_array;
    }

    bool IsShadow() const {
        return is_shadow;
    }

    bool IsBindless() const {
        return is_bindless;
    }

    std::pair<u32, u32> GetBindlessCBuf() const {
        return {static_cast<u32>(offset >> 32), static_cast<u32>(offset)};
    }

    bool operator<(const Sampler& rhs) const {
        return std::tie(index, offset, type, is_array, is_shadow, is_bindless) <
               std::tie(rhs.index, rhs.offset, rhs.type, rhs.is_array, rhs.is_shadow,
                        rhs.is_bindless);
    }

private:
    /// Offset in TSC memory from which to read the sampler object, as specified by the sampling
    /// instruction.
    std::size_t offset{};
    std::size_t index{}; ///< Value used to index into the generated GLSL sampler array.
    Tegra::Shader::TextureType type{}; ///< The type used to sample this texture (Texture2D, etc)
    bool is_array{};    ///< Whether the texture is being sampled as an array texture or not.
    bool is_shadow{};   ///< Whether the texture is being sampled as a depth texture or not.
    bool is_bindless{}; ///< Whether this sampler belongs to a bindless texture or not.
};

class Image final {
public:
    constexpr explicit Image(std::size_t offset, std::size_t index, Tegra::Shader::ImageType type,
                             std::optional<Tegra::Shader::ImageAtomicSize> size)
        : offset{offset}, index{index}, type{type}, is_bindless{false}, size{size} {}

    constexpr explicit Image(u32 cbuf_index, u32 cbuf_offset, std::size_t index,
                             Tegra::Shader::ImageType type,
                             std::optional<Tegra::Shader::ImageAtomicSize> size)
        : offset{(static_cast<u64>(cbuf_index) << 32) | cbuf_offset}, index{index}, type{type},
          is_bindless{true}, size{size} {}

    constexpr explicit Image(std::size_t offset, std::size_t index, Tegra::Shader::ImageType type,
                             bool is_bindless, bool is_written, bool is_read,
                             std::optional<Tegra::Shader::ImageAtomicSize> size)
        : offset{offset}, index{index}, type{type}, is_bindless{is_bindless},
          is_written{is_written}, is_read{is_read}, size{size} {}

    void MarkWrite() {
        is_written = true;
    }

    void MarkRead() {
        is_read = true;
    }

    void SetSize(Tegra::Shader::ImageAtomicSize size_) {
        size = size_;
    }

    constexpr std::size_t GetOffset() const {
        return offset;
    }

    constexpr std::size_t GetIndex() const {
        return index;
    }

    constexpr Tegra::Shader::ImageType GetType() const {
        return type;
    }

    constexpr bool IsBindless() const {
        return is_bindless;
    }

    constexpr bool IsWritten() const {
        return is_written;
    }

    constexpr bool IsRead() const {
        return is_read;
    }

    constexpr std::pair<u32, u32> GetBindlessCBuf() const {
        return {static_cast<u32>(offset >> 32), static_cast<u32>(offset)};
    }

    constexpr bool IsSizeKnown() const {
        return size.has_value();
    }

    constexpr Tegra::Shader::ImageAtomicSize GetSize() const {
        return size.value();
    }

    constexpr bool operator<(const Image& rhs) const {
        return std::tie(offset, index, type, size, is_bindless) <
               std::tie(rhs.offset, rhs.index, rhs.type, rhs.size, rhs.is_bindless);
    }

private:
    u64 offset{};
    std::size_t index{};
    Tegra::Shader::ImageType type{};
    bool is_bindless{};
    bool is_written{};
    bool is_read{};
    std::optional<Tegra::Shader::ImageAtomicSize> size{};
};

struct GlobalMemoryBase {
    u32 cbuf_index{};
    u32 cbuf_offset{};

    bool operator<(const GlobalMemoryBase& rhs) const {
        return std::tie(cbuf_index, cbuf_offset) < std::tie(rhs.cbuf_index, rhs.cbuf_offset);
    }
};

/// Parameters describing an arithmetic operation
struct MetaArithmetic {
    bool precise{}; ///< Whether the operation can be constraint or not
};

/// Parameters describing a texture sampler
struct MetaTexture {
    const Sampler& sampler;
    Node array;
    Node depth_compare;
    std::vector<Node> aoffi;
    Node bias;
    Node lod;
    Node component{};
    u32 element{};
};

struct MetaImage {
    const Image& image;
    std::vector<Node> values;
};

/// Parameters that modify an operation but are not part of any particular operand
using Meta =
    std::variant<MetaArithmetic, MetaTexture, MetaImage, MetaStackClass, Tegra::Shader::HalfType>;

/// Holds any kind of operation that can be done in the IR
class OperationNode final {
public:
    explicit OperationNode(OperationCode code) : OperationNode(code, Meta{}) {}

    explicit OperationNode(OperationCode code, Meta meta)
        : OperationNode(code, meta, std::vector<Node>{}) {}

    explicit OperationNode(OperationCode code, std::vector<Node> operands)
        : OperationNode(code, Meta{}, std::move(operands)) {}

    explicit OperationNode(OperationCode code, Meta meta, std::vector<Node> operands)
        : code{code}, meta{std::move(meta)}, operands{std::move(operands)} {}

    template <typename... Args>
    explicit OperationNode(OperationCode code, Meta meta, Args&&... operands)
        : code{code}, meta{std::move(meta)}, operands{operands...} {}

    OperationCode GetCode() const {
        return code;
    }

    const Meta& GetMeta() const {
        return meta;
    }

    std::size_t GetOperandsCount() const {
        return operands.size();
    }

    const Node& operator[](std::size_t operand_index) const {
        return operands.at(operand_index);
    }

private:
    OperationCode code{};
    Meta meta{};
    std::vector<Node> operands;
};

/// Encloses inside any kind of node that returns a boolean conditionally-executed code
class ConditionalNode final {
public:
    explicit ConditionalNode(Node condition, std::vector<Node>&& code)
        : condition{std::move(condition)}, code{std::move(code)} {}

    const Node& GetCondition() const {
        return condition;
    }

    const std::vector<Node>& GetCode() const {
        return code;
    }

private:
    Node condition;         ///< Condition to be satisfied
    std::vector<Node> code; ///< Code to execute
};

/// A general purpose register
class GprNode final {
public:
    explicit constexpr GprNode(Tegra::Shader::Register index) : index{index} {}

    u32 GetIndex() const {
        return static_cast<u32>(index);
    }

private:
    Tegra::Shader::Register index{};
};

/// A 32-bits value that represents an immediate value
class ImmediateNode final {
public:
    explicit constexpr ImmediateNode(u32 value) : value{value} {}

    u32 GetValue() const {
        return value;
    }

private:
    u32 value{};
};

/// One of Maxwell's internal flags
class InternalFlagNode final {
public:
    explicit constexpr InternalFlagNode(InternalFlag flag) : flag{flag} {}

    InternalFlag GetFlag() const {
        return flag;
    }

private:
    InternalFlag flag{};
};

/// A predicate register, it can be negated without additional nodes
class PredicateNode final {
public:
    explicit constexpr PredicateNode(Tegra::Shader::Pred index, bool negated)
        : index{index}, negated{negated} {}

    Tegra::Shader::Pred GetIndex() const {
        return index;
    }

    bool IsNegated() const {
        return negated;
    }

private:
    Tegra::Shader::Pred index{};
    bool negated{};
};

/// Attribute buffer memory (known as attributes or varyings in GLSL terms)
class AbufNode final {
public:
    // Initialize for standard attributes (index is explicit).
    explicit AbufNode(Tegra::Shader::Attribute::Index index, u32 element, Node buffer = {})
        : buffer{std::move(buffer)}, index{index}, element{element} {}

    // Initialize for physical attributes (index is a variable value).
    explicit AbufNode(Node physical_address, Node buffer = {})
        : physical_address{std::move(physical_address)}, buffer{std::move(buffer)} {}

    Tegra::Shader::Attribute::Index GetIndex() const {
        return index;
    }

    u32 GetElement() const {
        return element;
    }

    const Node& GetBuffer() const {
        return buffer;
    }

    bool IsPhysicalBuffer() const {
        return static_cast<bool>(physical_address);
    }

    const Node& GetPhysicalAddress() const {
        return physical_address;
    }

private:
    Node physical_address;
    Node buffer;
    Tegra::Shader::Attribute::Index index{};
    u32 element{};
};

/// Constant buffer node, usually mapped to uniform buffers in GLSL
class CbufNode final {
public:
    explicit CbufNode(u32 index, Node offset) : index{index}, offset{std::move(offset)} {}

    u32 GetIndex() const {
        return index;
    }

    const Node& GetOffset() const {
        return offset;
    }

private:
    u32 index{};
    Node offset;
};

/// Local memory node
class LmemNode final {
public:
    explicit LmemNode(Node address) : address{std::move(address)} {}

    const Node& GetAddress() const {
        return address;
    }

private:
    Node address;
};

/// Shared memory node
class SmemNode final {
public:
    explicit SmemNode(Node address) : address{std::move(address)} {}

    const Node& GetAddress() const {
        return address;
    }

private:
    Node address;
};

/// Global memory node
class GmemNode final {
public:
    explicit GmemNode(Node real_address, Node base_address, const GlobalMemoryBase& descriptor)
        : real_address{std::move(real_address)}, base_address{std::move(base_address)},
          descriptor{descriptor} {}

    const Node& GetRealAddress() const {
        return real_address;
    }

    const Node& GetBaseAddress() const {
        return base_address;
    }

    const GlobalMemoryBase& GetDescriptor() const {
        return descriptor;
    }

private:
    Node real_address;
    Node base_address;
    GlobalMemoryBase descriptor;
};

/// Commentary, can be dropped
class CommentNode final {
public:
    explicit CommentNode(std::string text) : text{std::move(text)} {}

    const std::string& GetText() const {
        return text;
    }

private:
    std::string text;
};

} // namespace VideoCommon::Shader
