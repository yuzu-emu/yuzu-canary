#ifdef _MSC_VER
#include <intrin.h>

#pragma intrinsic(_umul128)
#endif
#include "common/uint128.h"

namespace Common {
u128 Multiply64Into128(u64 a, u64 b) {
#ifdef _MSC_VER
    u128 result;
    result[0] = _umul128(a, b, &result[1]);
#else
    unsigned __int128 tmp = a;
    tmp *= b;
    u128 result;
    std::memcpy(&result, &tmp, sizeof(u128));
#endif
    return result;
}

std::pair<u64, u64> Divide128On64(u128 dividend, u64 divisor) {
    u64 remainder = dividend[0] % divisor;
    u64 accum = dividend[0] / divisor;
    if (dividend[1] == 0)
        return {accum, remainder};
    // We ignore dividend[1] / divisor as that overflows
    u64 first_segment = (dividend[1] % divisor) << 32;
    accum += (first_segment / divisor) << 32;
    u64 second_segment = (first_segment % divisor) << 32;
    accum += (second_segment / divisor);
    remainder += second_segment % divisor;
    if (remainder >= divisor) {
        accum++;
        remainder -= divisor;
    }
    return {accum, remainder};
}

} // namespace Common
