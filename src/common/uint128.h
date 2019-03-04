
#include <utility>
#include "common/common_types.h"

namespace Common {

// This function multiplies 2 u64 values and produces a u128 value;
u128 Multiply64Into128(u64 a, u64 b);

// This function divides a u128 by a u32 value and produces two u64 values:
// the result of division and the remainder
std::pair<u64, u64> Divide128On32(u128 dividend, u32 divisor);

} // namespace Common
