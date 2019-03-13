#ifdef _MSC_VER
#include <intrin.h>

#pragma intrinsic(_umul128)
#endif
#include <cstring>
#include "common/uint128.h"

namespace Common {

u128 Multiply64Into128(u64 a, u64 b) {
    u128 result;
#ifdef _MSC_VER
    result[0] = _umul128(a, b, &result[1]);
#else
    unsigned __int128 tmp = a;
    tmp *= b;
    std::memcpy(&result, &tmp, sizeof(u128));
#endif
    return result;
}

std::pair<u64, u64> Divide128On32(u128 dividend, u32 divisor) {
    u64 remainder = dividend[0] % divisor;
    u64 accum = dividend[0] / divisor;
    if (dividend[1] == 0)
        return {accum, remainder};
    // We ignore dividend[1] / divisor as that overflows
    const u64 first_segment = (dividend[1] % divisor) << 32;
    accum += (first_segment / divisor) << 32;
    const u64 second_segment = (first_segment % divisor) << 32;
    accum += (second_segment / divisor);
    remainder += second_segment % divisor;
    if (remainder >= divisor) {
        accum++;
        remainder -= divisor;
    }
    return {accum, remainder};
}

} // namespace Common
