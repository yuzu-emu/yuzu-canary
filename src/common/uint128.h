#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include "common/common_types.h"

namespace Common {

u128 Multiply64Into128(u64 a, u64 b);

std::pair<u64, u64> Divide128On64(u128 dividend, u64 divisor);

} // namespace Common
