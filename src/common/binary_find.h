// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>

namespace Common {

template <class ForwardIt, class T, class Compare = std::less<>>
ForwardIt BinaryFind(ForwardIt first, ForwardIt last, const T& value, Compare comp = {}) {
    // Note: BOTH type T and the type after ForwardIt is dereferenced
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare.
    // This is stricter than lower_bound requirement (see above)

    first = std::lower_bound(first, last, value, comp);
    return first != last && !comp(value, *first) ? first : last;
}

} // namespace Common
