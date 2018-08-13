// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#define _USE_MATH_DEFINES

#include <algorithm>
#include <cmath>
#include <vector>
#include "audio_core/algorithm/interpolate.h"
#include "common/common_types.h"
#include "common/logging/log.h"

namespace AudioCore {

/// The Lanczos kernel
static double Lanczos(size_t a, double x) {
    if (x == 0.0)
        return 1.0;
    const double px = M_PI * x;
    return a * std::sin(px) * std::sin(px / a) / (px * px);
}

std::vector<s16> Interpolate(InterpolationState& state, std::vector<s16> input, double ratio) {
    if (input.size() < 2)
        return {};

    if (ratio <= 0) {
        LOG_CRITICAL(Audio, "Nonsensical interpolation ratio {}", ratio);
        ratio = 1.0;
    }

    if (ratio != state.current_ratio) {
        const double cutoff_frequency = std::min(0.5 / ratio, 0.5 * ratio);
        state.nyquist = CascadingFilter::LowPass(std::clamp(cutoff_frequency, 0.0, 0.4), 3);
        state.current_ratio = ratio;
    }
    state.nyquist.Process(input);

    constexpr size_t taps = InterpolationState::lanczos_taps;
    const size_t num_frames = input.size() / 2;

    std::vector<s16> output;
    output.reserve(static_cast<size_t>(input.size() / ratio + 4));

    double& pos = state.position;
    auto& h = state.history;
    for (size_t i = 0; i < num_frames; ++i) {
        std::rotate(h.begin(), h.end() - 1, h.end());
        h[0][0] = input[i * 2 + 0];
        h[0][1] = input[i * 2 + 1];

        while (pos <= 1.0) {
            double l = 0.0;
            double r = 0.0;
            for (size_t j = 0; j < h.size(); j++) {
                l += Lanczos(taps, pos + j - taps + 1) * h[j][0];
                r += Lanczos(taps, pos + j - taps + 1) * h[j][1];
            }
            output.emplace_back(static_cast<s16>(std::clamp(l, -32768.0, 32767.0)));
            output.emplace_back(static_cast<s16>(std::clamp(r, -32768.0, 32767.0)));

            pos += ratio;
        }
        pos -= 1.0;
    }

    return output;
}

} // namespace AudioCore
