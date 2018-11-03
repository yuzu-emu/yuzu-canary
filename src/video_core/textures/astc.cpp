// Copyright 2016 The University of North Carolina at Chapel Hill
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Please send all BUG REPORTS to <pavel@cs.unc.edu>.
// <http://gamma.cs.unc.edu/FasTC/>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "video_core/textures/astc.h"

class BitStream {
public:
    explicit BitStream(unsigned char* ptr, int nBits = 0, int start_offset = 0)
        : m_NumBits(nBits), m_CurByte(ptr), m_NextBit(start_offset % 8) {}

    ~BitStream() = default;

    int GetBitsWritten() const {
        return m_BitsWritten;
    }

    void WriteBitsR(unsigned int val, unsigned int nBits) {
        for (unsigned int i = 0; i < nBits; i++) {
            WriteBit((val >> (nBits - i - 1)) & 1);
        }
    }

    void WriteBits(unsigned int val, unsigned int nBits) {
        for (unsigned int i = 0; i < nBits; i++) {
            WriteBit((val >> i) & 1);
        }
    }

    int GetBitsRead() const {
        return m_BitsRead;
    }

    int ReadBit() {

        int bit = *m_CurByte >> m_NextBit++;
        while (m_NextBit >= 8) {
            m_NextBit -= 8;
            m_CurByte++;
        }

        m_BitsRead++;
        return bit & 1;
    }

    unsigned int ReadBits(unsigned int nBits) {
        unsigned int ret = 0;
        for (unsigned int i = 0; i < nBits; i++) {
            ret |= (ReadBit() & 1) << i;
        }
        return ret;
    }

private:
    void WriteBit(int b) {

        if (done)
            return;

        const unsigned int mask = 1 << m_NextBit++;

        // clear the bit
        *m_CurByte &= ~mask;

        // Write the bit, if necessary
        if (b)
            *m_CurByte |= mask;

        // Next byte?
        if (m_NextBit >= 8) {
            m_CurByte += 1;
            m_NextBit = 0;
        }

        done = done || ++m_BitsWritten >= m_NumBits;
    }

    int m_BitsWritten = 0;
    const int m_NumBits;
    unsigned char* m_CurByte;
    int m_NextBit = 0;
    int m_BitsRead = 0;

    bool done = false;
};

template <typename IntType>
class Bits {
public:
    explicit Bits(const IntType& v) : m_Bits(v) {}

    Bits(const Bits&) = delete;
    Bits& operator=(const Bits&) = delete;

    uint8_t operator[](uint32_t bitPos) const {
        return static_cast<uint8_t>((m_Bits >> bitPos) & 1);
    }

    IntType operator()(uint32_t start, uint32_t end) const {
        if (start == end) {
            return (*this)[start];
        } else if (start > end) {
            uint32_t t = start;
            start = end;
            end = t;
        }

        uint64_t mask = (1 << (end - start + 1)) - 1;
        return (m_Bits >> start) & mask;
    }

private:
    const IntType& m_Bits;
};

enum EIntegerEncoding { eIntegerEncoding_JustBits, eIntegerEncoding_Quint, eIntegerEncoding_Trit };

class IntegerEncodedValue {
private:
    const EIntegerEncoding m_Encoding;
    const uint32_t m_NumBits;
    uint32_t m_BitValue;
    union {
        uint32_t m_QuintValue;
        uint32_t m_TritValue;
    };

public:
    // Jank, but we're not doing any heavy lifting in this class, so it's
    // probably OK. It allows us to use these in std::vectors...
    IntegerEncodedValue& operator=(const IntegerEncodedValue& other) {
        new (this) IntegerEncodedValue(other);
        return *this;
    }

    IntegerEncodedValue(EIntegerEncoding encoding, uint32_t numBits)
        : m_Encoding(encoding), m_NumBits(numBits) {}

    EIntegerEncoding GetEncoding() const {
        return m_Encoding;
    }
    uint32_t BaseBitLength() const {
        return m_NumBits;
    }

    uint32_t GetBitValue() const {
        return m_BitValue;
    }
    void SetBitValue(uint32_t val) {
        m_BitValue = val;
    }

    uint32_t GetTritValue() const {
        return m_TritValue;
    }
    void SetTritValue(uint32_t val) {
        m_TritValue = val;
    }

    uint32_t GetQuintValue() const {
        return m_QuintValue;
    }
    void SetQuintValue(uint32_t val) {
        m_QuintValue = val;
    }

    bool MatchesEncoding(const IntegerEncodedValue& other) const {
        return m_Encoding == other.m_Encoding && m_NumBits == other.m_NumBits;
    }

    // Returns the number of bits required to encode nVals values.
    uint32_t GetBitLength(uint32_t nVals) const {
        uint32_t totalBits = m_NumBits * nVals;
        if (m_Encoding == eIntegerEncoding_Trit) {
            totalBits += (nVals * 8 + 4) / 5;
        } else if (m_Encoding == eIntegerEncoding_Quint) {
            totalBits += (nVals * 7 + 2) / 3;
        }
        return totalBits;
    }

    // Count the number of bits set in a number.
    static inline uint32_t Popcnt(uint32_t n) {
        uint32_t c;
        for (c = 0; n; c++) {
            n &= n - 1;
        }
        return c;
    }

    // Returns a new instance of this struct that corresponds to the
    // can take no more than maxval values
    static IntegerEncodedValue CreateEncoding(uint32_t maxVal) {
        while (maxVal > 0) {
            uint32_t check = maxVal + 1;

            // Is maxVal a power of two?
            if (!(check & (check - 1))) {
                return IntegerEncodedValue(eIntegerEncoding_JustBits, Popcnt(maxVal));
            }

            // Is maxVal of the type 3*2^n - 1?
            if ((check % 3 == 0) && !((check / 3) & ((check / 3) - 1))) {
                return IntegerEncodedValue(eIntegerEncoding_Trit, Popcnt(check / 3 - 1));
            }

            // Is maxVal of the type 5*2^n - 1?
            if ((check % 5 == 0) && !((check / 5) & ((check / 5) - 1))) {
                return IntegerEncodedValue(eIntegerEncoding_Quint, Popcnt(check / 5 - 1));
            }

            // Apparently it can't be represented with a bounded integer sequence...
            // just iterate.
            maxVal--;
        }
        return IntegerEncodedValue(eIntegerEncoding_JustBits, 0);
    }

    // Fills result with the values that are encoded in the given
    // bitstream. We must know beforehand what the maximum possible
    // value is, and how many values we're decoding.
    static void DecodeIntegerSequence(std::vector<IntegerEncodedValue>& result, BitStream& bits,
                                      uint32_t maxRange, uint32_t nValues) {
        // Determine encoding parameters
        IntegerEncodedValue val = IntegerEncodedValue::CreateEncoding(maxRange);

        // Start decoding
        uint32_t nValsDecoded = 0;
        while (nValsDecoded < nValues) {
            switch (val.GetEncoding()) {
            case eIntegerEncoding_Quint:
                DecodeQuintBlock(bits, result, val.BaseBitLength());
                nValsDecoded += 3;
                break;

            case eIntegerEncoding_Trit:
                DecodeTritBlock(bits, result, val.BaseBitLength());
                nValsDecoded += 5;
                break;

            case eIntegerEncoding_JustBits:
                val.SetBitValue(bits.ReadBits(val.BaseBitLength()));
                result.push_back(val);
                nValsDecoded++;
                break;
            }
        }
    }

private:
    static void DecodeTritBlock(BitStream& bits, std::vector<IntegerEncodedValue>& result,
                                uint32_t nBitsPerValue) {
        // Implement the algorithm in section C.2.12
        uint32_t m[5];
        uint32_t t[5];
        uint32_t T;

        // Read the trit encoded block according to
        // table C.2.14
        m[0] = bits.ReadBits(nBitsPerValue);
        T = bits.ReadBits(2);
        m[1] = bits.ReadBits(nBitsPerValue);
        T |= bits.ReadBits(2) << 2;
        m[2] = bits.ReadBits(nBitsPerValue);
        T |= bits.ReadBit() << 4;
        m[3] = bits.ReadBits(nBitsPerValue);
        T |= bits.ReadBits(2) << 5;
        m[4] = bits.ReadBits(nBitsPerValue);
        T |= bits.ReadBit() << 7;

        uint32_t C = 0;

        Bits<uint32_t> Tb(T);
        if (Tb(2, 4) == 7) {
            C = (Tb(5, 7) << 2) | Tb(0, 1);
            t[4] = t[3] = 2;
        } else {
            C = Tb(0, 4);
            if (Tb(5, 6) == 3) {
                t[4] = 2;
                t[3] = Tb[7];
            } else {
                t[4] = Tb[7];
                t[3] = Tb(5, 6);
            }
        }

        Bits<uint32_t> Cb(C);
        if (Cb(0, 1) == 3) {
            t[2] = 2;
            t[1] = Cb[4];
            t[0] = (Cb[3] << 1) | (Cb[2] & ~Cb[3]);
        } else if (Cb(2, 3) == 3) {
            t[2] = 2;
            t[1] = 2;
            t[0] = Cb(0, 1);
        } else {
            t[2] = Cb[4];
            t[1] = Cb(2, 3);
            t[0] = (Cb[1] << 1) | (Cb[0] & ~Cb[1]);
        }

        for (uint32_t i = 0; i < 5; i++) {
            IntegerEncodedValue val(eIntegerEncoding_Trit, nBitsPerValue);
            val.SetBitValue(m[i]);
            val.SetTritValue(t[i]);
            result.push_back(val);
        }
    }

    static void DecodeQuintBlock(BitStream& bits, std::vector<IntegerEncodedValue>& result,
                                 uint32_t nBitsPerValue) {
        // Implement the algorithm in section C.2.12
        uint32_t m[3];
        uint32_t q[3];
        uint32_t Q;

        // Read the trit encoded block according to
        // table C.2.15
        m[0] = bits.ReadBits(nBitsPerValue);
        Q = bits.ReadBits(3);
        m[1] = bits.ReadBits(nBitsPerValue);
        Q |= bits.ReadBits(2) << 3;
        m[2] = bits.ReadBits(nBitsPerValue);
        Q |= bits.ReadBits(2) << 5;

        Bits<uint32_t> Qb(Q);
        if (Qb(1, 2) == 3 && Qb(5, 6) == 0) {
            q[0] = q[1] = 4;
            q[2] = (Qb[0] << 2) | ((Qb[4] & ~Qb[0]) << 1) | (Qb[3] & ~Qb[0]);
        } else {
            uint32_t C = 0;
            if (Qb(1, 2) == 3) {
                q[2] = 4;
                C = (Qb(3, 4) << 3) | ((~Qb(5, 6) & 3) << 1) | Qb[0];
            } else {
                q[2] = Qb(5, 6);
                C = Qb(0, 4);
            }

            Bits<uint32_t> Cb(C);
            if (Cb(0, 2) == 5) {
                q[1] = 4;
                q[0] = Cb(3, 4);
            } else {
                q[1] = Cb(3, 4);
                q[0] = Cb(0, 2);
            }
        }

        for (uint32_t i = 0; i < 3; i++) {
            IntegerEncodedValue val(eIntegerEncoding_Quint, nBitsPerValue);
            val.m_BitValue = m[i];
            val.m_QuintValue = q[i];
            result.push_back(val);
        }
    }
};

namespace ASTCC {

struct TexelWeightParams {
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    bool m_bDualPlane = false;
    uint32_t m_MaxWeight = 0;
    bool m_bError = false;
    bool m_bVoidExtentLDR = false;
    bool m_bVoidExtentHDR = false;

    uint32_t GetPackedBitSize() const {
        // How many indices do we have?
        uint32_t nIdxs = m_Height * m_Width;
        if (m_bDualPlane) {
            nIdxs *= 2;
        }

        return IntegerEncodedValue::CreateEncoding(m_MaxWeight).GetBitLength(nIdxs);
    }

    uint32_t GetNumWeightValues() const {
        uint32_t ret = m_Width * m_Height;
        if (m_bDualPlane) {
            ret *= 2;
        }
        return ret;
    }
};

static TexelWeightParams DecodeBlockInfo(BitStream& strm) {
    TexelWeightParams params;

    // Read the entire block mode all at once
    uint16_t modeBits = strm.ReadBits(11);

    // Does this match the void extent block mode?
    if ((modeBits & 0x01FF) == 0x1FC) {
        if (modeBits & 0x200) {
            params.m_bVoidExtentHDR = true;
        } else {
            params.m_bVoidExtentLDR = true;
        }

        // Next two bits must be one.
        if (!(modeBits & 0x400) || !strm.ReadBit()) {
            params.m_bError = true;
        }

        return params;
    }

    // First check if the last four bits are zero
    if ((modeBits & 0xF) == 0) {
        params.m_bError = true;
        return params;
    }

    // If the last two bits are zero, then if bits
    // [6-8] are all ones, this is also reserved.
    if ((modeBits & 0x3) == 0 && (modeBits & 0x1C0) == 0x1C0) {
        params.m_bError = true;
        return params;
    }

    // Otherwise, there is no error... Figure out the layout
    // of the block mode. Layout is determined by a number
    // between 0 and 9 corresponding to table C.2.8 of the
    // ASTC spec.
    uint32_t layout = 0;

    if ((modeBits & 0x1) || (modeBits & 0x2)) {
        // layout is in [0-4]
        if (modeBits & 0x8) {
            // layout is in [2-4]
            if (modeBits & 0x4) {
                // layout is in [3-4]
                if (modeBits & 0x100) {
                    layout = 4;
                } else {
                    layout = 3;
                }
            } else {
                layout = 2;
            }
        } else {
            // layout is in [0-1]
            if (modeBits & 0x4) {
                layout = 1;
            } else {
                layout = 0;
            }
        }
    } else {
        // layout is in [5-9]
        if (modeBits & 0x100) {
            // layout is in [7-9]
            if (modeBits & 0x80) {
                // layout is in [7-8]
                assert((modeBits & 0x40) == 0U);
                if (modeBits & 0x20) {
                    layout = 8;
                } else {
                    layout = 7;
                }
            } else {
                layout = 9;
            }
        } else {
            // layout is in [5-6]
            if (modeBits & 0x80) {
                layout = 6;
            } else {
                layout = 5;
            }
        }
    }

    assert(layout < 10);

    // Determine R
    uint32_t R = !!(modeBits & 0x10);
    if (layout < 5) {
        R |= (modeBits & 0x3) << 1;
    } else {
        R |= (modeBits & 0xC) >> 1;
    }
    assert(2 <= R && R <= 7);

    // Determine width & height
    switch (layout) {
    case 0: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 7) & 0x3;
        params.m_Width = B + 4;
        params.m_Height = A + 2;
        break;
    }

    case 1: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 7) & 0x3;
        params.m_Width = B + 8;
        params.m_Height = A + 2;
        break;
    }

    case 2: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 7) & 0x3;
        params.m_Width = A + 2;
        params.m_Height = B + 8;
        break;
    }

    case 3: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 7) & 0x1;
        params.m_Width = A + 2;
        params.m_Height = B + 6;
        break;
    }

    case 4: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 7) & 0x1;
        params.m_Width = B + 2;
        params.m_Height = A + 2;
        break;
    }

    case 5: {
        uint32_t A = (modeBits >> 5) & 0x3;
        params.m_Width = 12;
        params.m_Height = A + 2;
        break;
    }

    case 6: {
        uint32_t A = (modeBits >> 5) & 0x3;
        params.m_Width = A + 2;
        params.m_Height = 12;
        break;
    }

    case 7: {
        params.m_Width = 6;
        params.m_Height = 10;
        break;
    }

    case 8: {
        params.m_Width = 10;
        params.m_Height = 6;
        break;
    }

    case 9: {
        uint32_t A = (modeBits >> 5) & 0x3;
        uint32_t B = (modeBits >> 9) & 0x3;
        params.m_Width = A + 6;
        params.m_Height = B + 6;
        break;
    }

    default:
        assert(!"Don't know this layout...");
        params.m_bError = true;
        break;
    }

    // Determine whether or not we're using dual planes
    // and/or high precision layouts.
    bool D = (layout != 9) && (modeBits & 0x400);
    bool H = (layout != 9) && (modeBits & 0x200);

    if (H) {
        const uint32_t maxWeights[6] = {9, 11, 15, 19, 23, 31};
        params.m_MaxWeight = maxWeights[R - 2];
    } else {
        const uint32_t maxWeights[6] = {1, 2, 3, 4, 5, 7};
        params.m_MaxWeight = maxWeights[R - 2];
    }

    params.m_bDualPlane = D;

    return params;
}

static void FillVoidExtentLDR(BitStream& strm, uint32_t* const outBuf, uint32_t blockWidth,
                              uint32_t blockHeight) {
    // Don't actually care about the void extent, just read the bits...
    for (int i = 0; i < 4; ++i) {
        strm.ReadBits(13);
    }

    // Decode the RGBA components and renormalize them to the range [0, 255]
    uint16_t r = strm.ReadBits(16);
    uint16_t g = strm.ReadBits(16);
    uint16_t b = strm.ReadBits(16);
    uint16_t a = strm.ReadBits(16);

    uint32_t rgba = (r >> 8) | (g & 0xFF00) | (static_cast<uint32_t>(b) & 0xFF00) << 8 |
                    (static_cast<uint32_t>(a) & 0xFF00) << 16;

    for (uint32_t j = 0; j < blockHeight; j++) {
        for (uint32_t i = 0; i < blockWidth; i++) {
            outBuf[j * blockWidth + i] = rgba;
        }
    }
}

static void FillError(uint32_t* outBuf, uint32_t blockWidth, uint32_t blockHeight) {
    for (uint32_t j = 0; j < blockHeight; j++) {
        for (uint32_t i = 0; i < blockWidth; i++) {
            outBuf[j * blockWidth + i] = 0xFFFF00FF;
        }
    }
}

// Replicates low numBits such that [(toBit - 1):(toBit - 1 - fromBit)]
// is the same as [(numBits - 1):0] and repeats all the way down.
template <typename IntType>
static IntType Replicate(const IntType& val, uint32_t numBits, uint32_t toBit) {
    if (numBits == 0)
        return 0;
    if (toBit == 0)
        return 0;
    IntType v = val & ((1 << numBits) - 1);
    IntType res = v;
    uint32_t reslen = numBits;
    while (reslen < toBit) {
        uint32_t comp = 0;
        if (numBits > toBit - reslen) {
            uint32_t newshift = toBit - reslen;
            comp = numBits - newshift;
            numBits = newshift;
        }
        res <<= numBits;
        res |= v >> comp;
        reslen += numBits;
    }
    return res;
}

class Pixel {
protected:
    using ChannelType = int16_t;
    uint8_t m_BitDepth[4] = {8, 8, 8, 8};
    int16_t color[4] = {};

public:
    Pixel() = default;
    Pixel(ChannelType a, ChannelType r, ChannelType g, ChannelType b, unsigned bitDepth = 8)
        : m_BitDepth{uint8_t(bitDepth), uint8_t(bitDepth), uint8_t(bitDepth), uint8_t(bitDepth)},
          color{a, r, g, b} {}

    // Changes the depth of each pixel. This scales the values to
    // the appropriate bit depth by either truncating the least
    // significant bits when going from larger to smaller bit depth
    // or by repeating the most significant bits when going from
    // smaller to larger bit depths.
    void ChangeBitDepth(const uint8_t (&depth)[4]) {
        for (uint32_t i = 0; i < 4; i++) {
            Component(i) = ChangeBitDepth(Component(i), m_BitDepth[i], depth[i]);
            m_BitDepth[i] = depth[i];
        }
    }

    template <typename IntType>
    static float ConvertChannelToFloat(IntType channel, uint8_t bitDepth) {
        float denominator = static_cast<float>((1 << bitDepth) - 1);
        return static_cast<float>(channel) / denominator;
    }

    // Changes the bit depth of a single component. See the comment
    // above for how we do this.
    static ChannelType ChangeBitDepth(Pixel::ChannelType val, uint8_t oldDepth, uint8_t newDepth) {
        assert(newDepth <= 8);
        assert(oldDepth <= 8);

        if (oldDepth == newDepth) {
            // Do nothing
            return val;
        } else if (oldDepth == 0 && newDepth != 0) {
            return (1 << newDepth) - 1;
        } else if (newDepth > oldDepth) {
            return Replicate(val, oldDepth, newDepth);
        } else {
            // oldDepth > newDepth
            if (newDepth == 0) {
                return 0xFF;
            } else {
                uint8_t bitsWasted = oldDepth - newDepth;
                uint16_t v = static_cast<uint16_t>(val);
                v = (v + (1 << (bitsWasted - 1))) >> bitsWasted;
                v = ::std::min<uint16_t>(::std::max<uint16_t>(0, v), (1 << newDepth) - 1);
                return static_cast<uint8_t>(v);
            }
        }

        assert(!"We shouldn't get here.");
        return 0;
    }

    const ChannelType& A() const {
        return color[0];
    }
    ChannelType& A() {
        return color[0];
    }
    const ChannelType& R() const {
        return color[1];
    }
    ChannelType& R() {
        return color[1];
    }
    const ChannelType& G() const {
        return color[2];
    }
    ChannelType& G() {
        return color[2];
    }
    const ChannelType& B() const {
        return color[3];
    }
    ChannelType& B() {
        return color[3];
    }
    const ChannelType& Component(uint32_t idx) const {
        return color[idx];
    }
    ChannelType& Component(uint32_t idx) {
        return color[idx];
    }

    void GetBitDepth(uint8_t (&outDepth)[4]) const {
        for (int i = 0; i < 4; i++) {
            outDepth[i] = m_BitDepth[i];
        }
    }

    // Take all of the components, transform them to their 8-bit variants,
    // and then pack each channel into an R8G8B8A8 32-bit integer. We assume
    // that the architecture is little-endian, so the alpha channel will end
    // up in the most-significant byte.
    uint32_t Pack() const {
        Pixel eightBit(*this);
        const uint8_t eightBitDepth[4] = {8, 8, 8, 8};
        eightBit.ChangeBitDepth(eightBitDepth);

        uint32_t r = 0;
        r |= eightBit.A();
        r <<= 8;
        r |= eightBit.B();
        r <<= 8;
        r |= eightBit.G();
        r <<= 8;
        r |= eightBit.R();
        return r;
    }

    // Clamps the pixel to the range [0,255]
    void ClampByte() {
        for (uint32_t i = 0; i < 4; i++) {
            color[i] = (color[i] < 0) ? 0 : ((color[i] > 255) ? 255 : color[i]);
        }
    }

    void MakeOpaque() {
        A() = 255;
    }
};

static void DecodeColorValues(uint32_t* out, uint8_t* data, const uint32_t* modes,
                              const uint32_t nPartitions, const uint32_t nBitsForColorData) {
    // First figure out how many color values we have
    uint32_t nValues = 0;
    for (uint32_t i = 0; i < nPartitions; i++) {
        nValues += ((modes[i] >> 2) + 1) << 1;
    }

    // Then based on the number of values and the remaining number of bits,
    // figure out the max value for each of them...
    uint32_t range = 256;
    while (--range > 0) {
        IntegerEncodedValue val = IntegerEncodedValue::CreateEncoding(range);
        uint32_t bitLength = val.GetBitLength(nValues);
        if (bitLength <= nBitsForColorData) {
            // Find the smallest possible range that matches the given encoding
            while (--range > 0) {
                IntegerEncodedValue newval = IntegerEncodedValue::CreateEncoding(range);
                if (!newval.MatchesEncoding(val)) {
                    break;
                }
            }

            // Return to last matching range.
            range++;
            break;
        }
    }

    // We now have enough to decode our integer sequence.
    std::vector<IntegerEncodedValue> decodedColorValues;
    BitStream colorStream(data);
    IntegerEncodedValue::DecodeIntegerSequence(decodedColorValues, colorStream, range, nValues);

    // Once we have the decoded values, we need to dequantize them to the 0-255 range
    // This procedure is outlined in ASTC spec C.2.13
    uint32_t outIdx = 0;
    for (auto itr = decodedColorValues.begin(); itr != decodedColorValues.end(); ++itr) {
        // Have we already decoded all that we need?
        if (outIdx >= nValues) {
            break;
        }

        const IntegerEncodedValue& val = *itr;
        uint32_t bitlen = val.BaseBitLength();
        uint32_t bitval = val.GetBitValue();

        assert(bitlen >= 1);

        uint32_t A = 0, B = 0, C = 0, D = 0;
        // A is just the lsb replicated 9 times.
        A = Replicate(bitval & 1, 1, 9);

        switch (val.GetEncoding()) {
        // Replicate bits
        case eIntegerEncoding_JustBits:
            out[outIdx++] = Replicate(bitval, bitlen, 8);
            break;

        // Use algorithm in C.2.13
        case eIntegerEncoding_Trit: {

            D = val.GetTritValue();

            switch (bitlen) {
            case 1: {
                C = 204;
            } break;

            case 2: {
                C = 93;
                // B = b000b0bb0
                uint32_t b = (bitval >> 1) & 1;
                B = (b << 8) | (b << 4) | (b << 2) | (b << 1);
            } break;

            case 3: {
                C = 44;
                // B = cb000cbcb
                uint32_t cb = (bitval >> 1) & 3;
                B = (cb << 7) | (cb << 2) | cb;
            } break;

            case 4: {
                C = 22;
                // B = dcb000dcb
                uint32_t dcb = (bitval >> 1) & 7;
                B = (dcb << 6) | dcb;
            } break;

            case 5: {
                C = 11;
                // B = edcb000ed
                uint32_t edcb = (bitval >> 1) & 0xF;
                B = (edcb << 5) | (edcb >> 2);
            } break;

            case 6: {
                C = 5;
                // B = fedcb000f
                uint32_t fedcb = (bitval >> 1) & 0x1F;
                B = (fedcb << 4) | (fedcb >> 4);
            } break;

            default:
                assert(!"Unsupported trit encoding for color values!");
                break;
            } // switch(bitlen)
        }     // case eIntegerEncoding_Trit
        break;

        case eIntegerEncoding_Quint: {

            D = val.GetQuintValue();

            switch (bitlen) {
            case 1: {
                C = 113;
            } break;

            case 2: {
                C = 54;
                // B = b0000bb00
                uint32_t b = (bitval >> 1) & 1;
                B = (b << 8) | (b << 3) | (b << 2);
            } break;

            case 3: {
                C = 26;
                // B = cb0000cbc
                uint32_t cb = (bitval >> 1) & 3;
                B = (cb << 7) | (cb << 1) | (cb >> 1);
            } break;

            case 4: {
                C = 13;
                // B = dcb0000dc
                uint32_t dcb = (bitval >> 1) & 7;
                B = (dcb << 6) | (dcb >> 1);
            } break;

            case 5: {
                C = 6;
                // B = edcb0000e
                uint32_t edcb = (bitval >> 1) & 0xF;
                B = (edcb << 5) | (edcb >> 3);
            } break;

            default:
                assert(!"Unsupported quint encoding for color values!");
                break;
            } // switch(bitlen)
        }     // case eIntegerEncoding_Quint
        break;
        } // switch(val.GetEncoding())

        if (val.GetEncoding() != eIntegerEncoding_JustBits) {
            uint32_t T = D * C + B;
            T ^= A;
            T = (A & 0x80) | (T >> 2);
            out[outIdx++] = T;
        }
    }

    // Make sure that each of our values is in the proper range...
    for (uint32_t i = 0; i < nValues; i++) {
        assert(out[i] <= 255);
    }
}

static uint32_t UnquantizeTexelWeight(const IntegerEncodedValue& val) {
    uint32_t bitval = val.GetBitValue();
    uint32_t bitlen = val.BaseBitLength();

    uint32_t A = Replicate(bitval & 1, 1, 7);
    uint32_t B = 0, C = 0, D = 0;

    uint32_t result = 0;
    switch (val.GetEncoding()) {
    case eIntegerEncoding_JustBits:
        result = Replicate(bitval, bitlen, 6);
        break;

    case eIntegerEncoding_Trit: {
        D = val.GetTritValue();
        assert(D < 3);

        switch (bitlen) {
        case 0: {
            uint32_t results[3] = {0, 32, 63};
            result = results[D];
        } break;

        case 1: {
            C = 50;
        } break;

        case 2: {
            C = 23;
            uint32_t b = (bitval >> 1) & 1;
            B = (b << 6) | (b << 2) | b;
        } break;

        case 3: {
            C = 11;
            uint32_t cb = (bitval >> 1) & 3;
            B = (cb << 5) | cb;
        } break;

        default:
            assert(!"Invalid trit encoding for texel weight");
            break;
        }
    } break;

    case eIntegerEncoding_Quint: {
        D = val.GetQuintValue();
        assert(D < 5);

        switch (bitlen) {
        case 0: {
            uint32_t results[5] = {0, 16, 32, 47, 63};
            result = results[D];
        } break;

        case 1: {
            C = 28;
        } break;

        case 2: {
            C = 13;
            uint32_t b = (bitval >> 1) & 1;
            B = (b << 6) | (b << 1);
        } break;

        default:
            assert(!"Invalid quint encoding for texel weight");
            break;
        }
    } break;
    }

    if (val.GetEncoding() != eIntegerEncoding_JustBits && bitlen > 0) {
        // Decode the value...
        result = D * C + B;
        result ^= A;
        result = (A & 0x20) | (result >> 2);
    }

    assert(result < 64);

    // Change from [0,63] to [0,64]
    if (result > 32) {
        result += 1;
    }

    return result;
}

static void UnquantizeTexelWeights(uint32_t out[2][144],
                                   const std::vector<IntegerEncodedValue>& weights,
                                   const TexelWeightParams& params, const uint32_t blockWidth,
                                   const uint32_t blockHeight) {
    uint32_t weightIdx = 0;
    uint32_t unquantized[2][144];

    for (auto itr = weights.begin(); itr != weights.end(); ++itr) {
        unquantized[0][weightIdx] = UnquantizeTexelWeight(*itr);

        if (params.m_bDualPlane) {
            ++itr;
            unquantized[1][weightIdx] = UnquantizeTexelWeight(*itr);
            if (itr == weights.end()) {
                break;
            }
        }

        if (++weightIdx >= (params.m_Width * params.m_Height))
            break;
    }

    // Do infill if necessary (Section C.2.18) ...
    uint32_t Ds = (1024 + (blockWidth / 2)) / (blockWidth - 1);
    uint32_t Dt = (1024 + (blockHeight / 2)) / (blockHeight - 1);

    const uint32_t kPlaneScale = params.m_bDualPlane ? 2U : 1U;
    for (uint32_t plane = 0; plane < kPlaneScale; plane++)
        for (uint32_t t = 0; t < blockHeight; t++)
            for (uint32_t s = 0; s < blockWidth; s++) {
                uint32_t cs = Ds * s;
                uint32_t ct = Dt * t;

                uint32_t gs = (cs * (params.m_Width - 1) + 32) >> 6;
                uint32_t gt = (ct * (params.m_Height - 1) + 32) >> 6;

                uint32_t js = gs >> 4;
                uint32_t fs = gs & 0xF;

                uint32_t jt = gt >> 4;
                uint32_t ft = gt & 0x0F;

                uint32_t w11 = (fs * ft + 8) >> 4;
                uint32_t w10 = ft - w11;
                uint32_t w01 = fs - w11;
                uint32_t w00 = 16 - fs - ft + w11;

                uint32_t v0 = js + jt * params.m_Width;

#define FIND_TEXEL(tidx, bidx)                                                                     \
    uint32_t p##bidx = 0;                                                                          \
    do {                                                                                           \
        if ((tidx) < (params.m_Width * params.m_Height)) {                                         \
            p##bidx = unquantized[plane][(tidx)];                                                  \
        }                                                                                          \
    } while (0)

                FIND_TEXEL(v0, 00);
                FIND_TEXEL(v0 + 1, 01);
                FIND_TEXEL(v0 + params.m_Width, 10);
                FIND_TEXEL(v0 + params.m_Width + 1, 11);

#undef FIND_TEXEL

                out[plane][t * blockWidth + s] =
                    (p00 * w00 + p01 * w01 + p10 * w10 + p11 * w11 + 8) >> 4;
            }
}

// Transfers a bit as described in C.2.14
static inline void BitTransferSigned(int32_t& a, int32_t& b) {
    b >>= 1;
    b |= a & 0x80;
    a >>= 1;
    a &= 0x3F;
    if (a & 0x20)
        a -= 0x40;
}

// Adds more precision to the blue channel as described
// in C.2.14
static inline Pixel BlueContract(int32_t a, int32_t r, int32_t g, int32_t b) {
    return Pixel(static_cast<int16_t>(a), static_cast<int16_t>((r + b) >> 1),
                 static_cast<int16_t>((g + b) >> 1), static_cast<int16_t>(b));
}

// Partition selection functions as specified in
// C.2.21
static inline uint32_t hash52(uint32_t p) {
    p ^= p >> 15;
    p -= p << 17;
    p += p << 7;
    p += p << 4;
    p ^= p >> 5;
    p += p << 16;
    p ^= p >> 7;
    p ^= p >> 3;
    p ^= p << 6;
    p ^= p >> 17;
    return p;
}

static uint32_t SelectPartition(int32_t seed, int32_t x, int32_t y, int32_t z,
                                int32_t partitionCount, int32_t smallBlock) {
    if (1 == partitionCount)
        return 0;

    if (smallBlock) {
        x <<= 1;
        y <<= 1;
        z <<= 1;
    }

    seed += (partitionCount - 1) * 1024;

    uint32_t rnum = hash52(static_cast<uint32_t>(seed));
    uint8_t seed1 = static_cast<uint8_t>(rnum & 0xF);
    uint8_t seed2 = static_cast<uint8_t>((rnum >> 4) & 0xF);
    uint8_t seed3 = static_cast<uint8_t>((rnum >> 8) & 0xF);
    uint8_t seed4 = static_cast<uint8_t>((rnum >> 12) & 0xF);
    uint8_t seed5 = static_cast<uint8_t>((rnum >> 16) & 0xF);
    uint8_t seed6 = static_cast<uint8_t>((rnum >> 20) & 0xF);
    uint8_t seed7 = static_cast<uint8_t>((rnum >> 24) & 0xF);
    uint8_t seed8 = static_cast<uint8_t>((rnum >> 28) & 0xF);
    uint8_t seed9 = static_cast<uint8_t>((rnum >> 18) & 0xF);
    uint8_t seed10 = static_cast<uint8_t>((rnum >> 22) & 0xF);
    uint8_t seed11 = static_cast<uint8_t>((rnum >> 26) & 0xF);
    uint8_t seed12 = static_cast<uint8_t>(((rnum >> 30) | (rnum << 2)) & 0xF);

    seed1 *= seed1;
    seed2 *= seed2;
    seed3 *= seed3;
    seed4 *= seed4;
    seed5 *= seed5;
    seed6 *= seed6;
    seed7 *= seed7;
    seed8 *= seed8;
    seed9 *= seed9;
    seed10 *= seed10;
    seed11 *= seed11;
    seed12 *= seed12;

    int32_t sh1, sh2, sh3;
    if (seed & 1) {
        sh1 = (seed & 2) ? 4 : 5;
        sh2 = (partitionCount == 3) ? 6 : 5;
    } else {
        sh1 = (partitionCount == 3) ? 6 : 5;
        sh2 = (seed & 2) ? 4 : 5;
    }
    sh3 = (seed & 0x10) ? sh1 : sh2;

    seed1 >>= sh1;
    seed2 >>= sh2;
    seed3 >>= sh1;
    seed4 >>= sh2;
    seed5 >>= sh1;
    seed6 >>= sh2;
    seed7 >>= sh1;
    seed8 >>= sh2;
    seed9 >>= sh3;
    seed10 >>= sh3;
    seed11 >>= sh3;
    seed12 >>= sh3;

    int32_t a = seed1 * x + seed2 * y + seed11 * z + (rnum >> 14);
    int32_t b = seed3 * x + seed4 * y + seed12 * z + (rnum >> 10);
    int32_t c = seed5 * x + seed6 * y + seed9 * z + (rnum >> 6);
    int32_t d = seed7 * x + seed8 * y + seed10 * z + (rnum >> 2);

    a &= 0x3F;
    b &= 0x3F;
    c &= 0x3F;
    d &= 0x3F;

    if (partitionCount < 4)
        d = 0;
    if (partitionCount < 3)
        c = 0;

    if (a >= b && a >= c && a >= d)
        return 0;
    else if (b >= c && b >= d)
        return 1;
    else if (c >= d)
        return 2;
    return 3;
}

static inline uint32_t Select2DPartition(int32_t seed, int32_t x, int32_t y, int32_t partitionCount,
                                         int32_t smallBlock) {
    return SelectPartition(seed, x, y, 0, partitionCount, smallBlock);
}

// Section C.2.14
static void ComputeEndpoints(Pixel& ep1, Pixel& ep2, const uint32_t*& colorValues,
                             uint32_t colorEndpointMode) {
#define READ_UINT_VALUES(N)                                                                        \
    uint32_t v[N];                                                                                 \
    for (uint32_t i = 0; i < N; i++) {                                                             \
        v[i] = *(colorValues++);                                                                   \
    }

#define READ_INT_VALUES(N)                                                                         \
    int32_t v[N];                                                                                  \
    for (uint32_t i = 0; i < N; i++) {                                                             \
        v[i] = static_cast<int32_t>(*(colorValues++));                                             \
    }

    switch (colorEndpointMode) {
    case 0: {
        READ_UINT_VALUES(2)
        ep1 = Pixel(0xFF, v[0], v[0], v[0]);
        ep2 = Pixel(0xFF, v[1], v[1], v[1]);
    } break;

    case 1: {
        READ_UINT_VALUES(2)
        uint32_t L0 = (v[0] >> 2) | (v[1] & 0xC0);
        uint32_t L1 = std::max(L0 + (v[1] & 0x3F), 0xFFU);
        ep1 = Pixel(0xFF, L0, L0, L0);
        ep2 = Pixel(0xFF, L1, L1, L1);
    } break;

    case 4: {
        READ_UINT_VALUES(4)
        ep1 = Pixel(v[2], v[0], v[0], v[0]);
        ep2 = Pixel(v[3], v[1], v[1], v[1]);
    } break;

    case 5: {
        READ_INT_VALUES(4)
        BitTransferSigned(v[1], v[0]);
        BitTransferSigned(v[3], v[2]);
        ep1 = Pixel(v[2], v[0], v[0], v[0]);
        ep2 = Pixel(v[2] + v[3], v[0] + v[1], v[0] + v[1], v[0] + v[1]);
        ep1.ClampByte();
        ep2.ClampByte();
    } break;

    case 6: {
        READ_UINT_VALUES(4)
        ep1 = Pixel(0xFF, v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8);
        ep2 = Pixel(0xFF, v[0], v[1], v[2]);
    } break;

    case 8: {
        READ_UINT_VALUES(6)
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            ep1 = Pixel(0xFF, v[0], v[2], v[4]);
            ep2 = Pixel(0xFF, v[1], v[3], v[5]);
        } else {
            ep1 = BlueContract(0xFF, v[1], v[3], v[5]);
            ep2 = BlueContract(0xFF, v[0], v[2], v[4]);
        }
    } break;

    case 9: {
        READ_INT_VALUES(6)
        BitTransferSigned(v[1], v[0]);
        BitTransferSigned(v[3], v[2]);
        BitTransferSigned(v[5], v[4]);
        if (v[1] + v[3] + v[5] >= 0) {
            ep1 = Pixel(0xFF, v[0], v[2], v[4]);
            ep2 = Pixel(0xFF, v[0] + v[1], v[2] + v[3], v[4] + v[5]);
        } else {
            ep1 = BlueContract(0xFF, v[0] + v[1], v[2] + v[3], v[4] + v[5]);
            ep2 = BlueContract(0xFF, v[0], v[2], v[4]);
        }
        ep1.ClampByte();
        ep2.ClampByte();
    } break;

    case 10: {
        READ_UINT_VALUES(6)
        ep1 = Pixel(v[4], v[0] * v[3] >> 8, v[1] * v[3] >> 8, v[2] * v[3] >> 8);
        ep2 = Pixel(v[5], v[0], v[1], v[2]);
    } break;

    case 12: {
        READ_UINT_VALUES(8)
        if (v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]) {
            ep1 = Pixel(v[6], v[0], v[2], v[4]);
            ep2 = Pixel(v[7], v[1], v[3], v[5]);
        } else {
            ep1 = BlueContract(v[7], v[1], v[3], v[5]);
            ep2 = BlueContract(v[6], v[0], v[2], v[4]);
        }
    } break;

    case 13: {
        READ_INT_VALUES(8)
        BitTransferSigned(v[1], v[0]);
        BitTransferSigned(v[3], v[2]);
        BitTransferSigned(v[5], v[4]);
        BitTransferSigned(v[7], v[6]);
        if (v[1] + v[3] + v[5] >= 0) {
            ep1 = Pixel(v[6], v[0], v[2], v[4]);
            ep2 = Pixel(v[7] + v[6], v[0] + v[1], v[2] + v[3], v[4] + v[5]);
        } else {
            ep1 = BlueContract(v[6] + v[7], v[0] + v[1], v[2] + v[3], v[4] + v[5]);
            ep2 = BlueContract(v[6], v[0], v[2], v[4]);
        }
        ep1.ClampByte();
        ep2.ClampByte();
    } break;

    default:
        assert(!"Unsupported color endpoint mode (is it HDR?)");
        break;
    }

#undef READ_UINT_VALUES
#undef READ_INT_VALUES
}

static void DecompressBlock(uint8_t inBuf[16], const uint32_t blockWidth,
                            const uint32_t blockHeight, uint32_t* outBuf) {
    BitStream strm(inBuf);
    TexelWeightParams weightParams = DecodeBlockInfo(strm);

    // Was there an error?
    if (weightParams.m_bError) {
        assert(!"Invalid block mode");
        FillError(outBuf, blockWidth, blockHeight);
        return;
    }

    if (weightParams.m_bVoidExtentLDR) {
        FillVoidExtentLDR(strm, outBuf, blockWidth, blockHeight);
        return;
    }

    if (weightParams.m_bVoidExtentHDR) {
        assert(!"HDR void extent blocks are unsupported!");
        FillError(outBuf, blockWidth, blockHeight);
        return;
    }

    if (weightParams.m_Width > blockWidth) {
        assert(!"Texel weight grid width should be smaller than block width");
        FillError(outBuf, blockWidth, blockHeight);
        return;
    }

    if (weightParams.m_Height > blockHeight) {
        assert(!"Texel weight grid height should be smaller than block height");
        FillError(outBuf, blockWidth, blockHeight);
        return;
    }

    // Read num partitions
    uint32_t nPartitions = strm.ReadBits(2) + 1;
    assert(nPartitions <= 4);

    if (nPartitions == 4 && weightParams.m_bDualPlane) {
        assert(!"Dual plane mode is incompatible with four partition blocks");
        FillError(outBuf, blockWidth, blockHeight);
        return;
    }

    // Based on the number of partitions, read the color endpoint mode for
    // each partition.

    // Determine partitions, partition index, and color endpoint modes
    int32_t planeIdx = -1;
    uint32_t partitionIndex;
    uint32_t colorEndpointMode[4] = {0, 0, 0, 0};

    // Define color data.
    uint8_t colorEndpointData[16];
    memset(colorEndpointData, 0, sizeof(colorEndpointData));
    BitStream colorEndpointStream(colorEndpointData, 16 * 8, 0);

    // Read extra config data...
    uint32_t baseCEM = 0;
    if (nPartitions == 1) {
        colorEndpointMode[0] = strm.ReadBits(4);
        partitionIndex = 0;
    } else {
        partitionIndex = strm.ReadBits(10);
        baseCEM = strm.ReadBits(6);
    }
    uint32_t baseMode = (baseCEM & 3);

    // Remaining bits are color endpoint data...
    uint32_t nWeightBits = weightParams.GetPackedBitSize();
    int32_t remainingBits = 128 - nWeightBits - strm.GetBitsRead();

    // Consider extra bits prior to texel data...
    uint32_t extraCEMbits = 0;
    if (baseMode) {
        switch (nPartitions) {
        case 2:
            extraCEMbits += 2;
            break;
        case 3:
            extraCEMbits += 5;
            break;
        case 4:
            extraCEMbits += 8;
            break;
        default:
            assert(false);
            break;
        }
    }
    remainingBits -= extraCEMbits;

    // Do we have a dual plane situation?
    uint32_t planeSelectorBits = 0;
    if (weightParams.m_bDualPlane) {
        planeSelectorBits = 2;
    }
    remainingBits -= planeSelectorBits;

    // Read color data...
    uint32_t colorDataBits = remainingBits;
    while (remainingBits > 0) {
        uint32_t nb = std::min(remainingBits, 8);
        uint32_t b = strm.ReadBits(nb);
        colorEndpointStream.WriteBits(b, nb);
        remainingBits -= 8;
    }

    // Read the plane selection bits
    planeIdx = strm.ReadBits(planeSelectorBits);

    // Read the rest of the CEM
    if (baseMode) {
        uint32_t extraCEM = strm.ReadBits(extraCEMbits);
        uint32_t CEM = (extraCEM << 6) | baseCEM;
        CEM >>= 2;

        bool C[4] = {0};
        for (uint32_t i = 0; i < nPartitions; i++) {
            C[i] = CEM & 1;
            CEM >>= 1;
        }

        uint8_t M[4] = {0};
        for (uint32_t i = 0; i < nPartitions; i++) {
            M[i] = CEM & 3;
            CEM >>= 2;
            assert(M[i] <= 3);
        }

        for (uint32_t i = 0; i < nPartitions; i++) {
            colorEndpointMode[i] = baseMode;
            if (!(C[i]))
                colorEndpointMode[i] -= 1;
            colorEndpointMode[i] <<= 2;
            colorEndpointMode[i] |= M[i];
        }
    } else if (nPartitions > 1) {
        uint32_t CEM = baseCEM >> 2;
        for (uint32_t i = 0; i < nPartitions; i++) {
            colorEndpointMode[i] = CEM;
        }
    }

    // Make sure everything up till here is sane.
    for (uint32_t i = 0; i < nPartitions; i++) {
        assert(colorEndpointMode[i] < 16);
    }
    assert(strm.GetBitsRead() + weightParams.GetPackedBitSize() == 128);

    // Decode both color data and texel weight data
    uint32_t colorValues[32]; // Four values, two endpoints, four maximum paritions
    DecodeColorValues(colorValues, colorEndpointData, colorEndpointMode, nPartitions,
                      colorDataBits);

    Pixel endpoints[4][2];
    const uint32_t* colorValuesPtr = colorValues;
    for (uint32_t i = 0; i < nPartitions; i++) {
        ComputeEndpoints(endpoints[i][0], endpoints[i][1], colorValuesPtr, colorEndpointMode[i]);
    }

    // Read the texel weight data..
    uint8_t texelWeightData[16];
    memcpy(texelWeightData, inBuf, sizeof(texelWeightData));

    // Reverse everything
    for (uint32_t i = 0; i < 8; i++) {
// Taken from http://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith64Bits
#define REVERSE_BYTE(b) (((b)*0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32
        unsigned char a = static_cast<unsigned char>(REVERSE_BYTE(texelWeightData[i]));
        unsigned char b = static_cast<unsigned char>(REVERSE_BYTE(texelWeightData[15 - i]));
#undef REVERSE_BYTE

        texelWeightData[i] = b;
        texelWeightData[15 - i] = a;
    }

    // Make sure that higher non-texel bits are set to zero
    const uint32_t clearByteStart = (weightParams.GetPackedBitSize() >> 3) + 1;
    texelWeightData[clearByteStart - 1] &= (1 << (weightParams.GetPackedBitSize() % 8)) - 1;
    memset(texelWeightData + clearByteStart, 0, 16 - clearByteStart);

    std::vector<IntegerEncodedValue> texelWeightValues;
    BitStream weightStream(texelWeightData);

    IntegerEncodedValue::DecodeIntegerSequence(texelWeightValues, weightStream,
                                               weightParams.m_MaxWeight,
                                               weightParams.GetNumWeightValues());

    // Blocks can be at most 12x12, so we can have as many as 144 weights
    uint32_t weights[2][144];
    UnquantizeTexelWeights(weights, texelWeightValues, weightParams, blockWidth, blockHeight);

    // Now that we have endpoints and weights, we can interpolate and generate
    // the proper decoding...
    for (uint32_t j = 0; j < blockHeight; j++)
        for (uint32_t i = 0; i < blockWidth; i++) {
            uint32_t partition = Select2DPartition(partitionIndex, i, j, nPartitions,
                                                   (blockHeight * blockWidth) < 32);
            assert(partition < nPartitions);

            Pixel p;
            for (uint32_t c = 0; c < 4; c++) {
                uint32_t C0 = endpoints[partition][0].Component(c);
                C0 = Replicate(C0, 8, 16);
                uint32_t C1 = endpoints[partition][1].Component(c);
                C1 = Replicate(C1, 8, 16);

                uint32_t plane = 0;
                if (weightParams.m_bDualPlane && (((planeIdx + 1) & 3) == c)) {
                    plane = 1;
                }

                uint32_t weight = weights[plane][j * blockWidth + i];
                uint32_t C = (C0 * (64 - weight) + C1 * weight + 32) / 64;
                if (C == 65535) {
                    p.Component(c) = 255;
                } else {
                    double Cf = static_cast<double>(C);
                    p.Component(c) = static_cast<uint16_t>(255.0 * (Cf / 65536.0) + 0.5);
                }
            }

            outBuf[j * blockWidth + i] = p.Pack();
        }
}

} // namespace ASTCC

namespace Tegra::Texture::ASTC {

std::vector<uint8_t> Decompress(std::vector<uint8_t>& data, uint32_t width, uint32_t height,
                                uint32_t depth, uint32_t block_width, uint32_t block_height) {
    uint32_t blockIdx = 0;
    std::vector<uint8_t> outData(height * width * depth * 4);
    for (uint32_t k = 0; k < depth; k++) {
        for (uint32_t j = 0; j < height; j += block_height) {
            for (uint32_t i = 0; i < width; i += block_width) {

                uint8_t* blockPtr = data.data() + blockIdx * 16;

                // Blocks can be at most 12x12
                uint32_t uncompData[144];
                ASTCC::DecompressBlock(blockPtr, block_width, block_height, uncompData);

                uint32_t decompWidth = std::min(block_width, width - i);
                uint32_t decompHeight = std::min(block_height, height - j);

                uint8_t* outRow = outData.data() + (j * width + i) * 4;
                for (uint32_t jj = 0; jj < decompHeight; jj++) {
                    memcpy(outRow + jj * width * 4, uncompData + jj * block_width, decompWidth * 4);
                }

                blockIdx++;
            }
        }
    }

    return outData;
}

} // namespace Tegra::Texture::ASTC
