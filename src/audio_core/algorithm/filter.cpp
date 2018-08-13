// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#define _USE_MATH_DEFINES

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "audio_core/algorithm/filter.h"
#include "common/common_types.h"

namespace AudioCore {

Filter Filter::LowPass(double cutoff, double Q) {
    const double w0 = 2.0 * M_PI * cutoff;
    const double sin_w0 = std::sin(w0);
    const double cos_w0 = std::cos(w0);
    const double alpha = sin_w0 / (2 * Q);

    const double a0 = 1 + alpha;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1 - alpha;
    const double b0 = 0.5 * (1 - cos_w0);
    const double b1 = 1.0 * (1 - cos_w0);
    const double b2 = 0.5 * (1 - cos_w0);

    return {a0, a1, a2, b0, b1, b2};
}

Filter::Filter() : Filter(1.0, 0.0, 0.0, 1.0, 0.0, 0.0) {}

Filter::Filter(double a0, double a1, double a2, double b0, double b1, double b2)
    : a1(a1 / a0), a2(a2 / a0), b0(b0 / a0), b1(b1 / a0), b2(b2 / a0) {}

void Filter::Process(std::vector<s16>& signal) {
    const size_t num_frames = signal.size() / 2;
    for (size_t i = 0; i < num_frames; i++) {
        std::rotate(in.begin(), in.end() - 1, in.end());
        std::rotate(out.begin(), out.end() - 1, out.end());

        for (size_t ch = 0; ch < channel_count; ch++) {
            in[0][ch] = signal[i * channel_count + ch];

            out[0][ch] = b0 * in[0][ch] + b1 * in[1][ch] + b2 * in[2][ch] - a1 * out[1][ch] -
                         a2 * out[2][ch];

            signal[i * 2 + ch] = std::clamp(out[0][ch], -32768.0, 32767.0);
        }
    }
}

/// Calculates the appropriate Q for each biquad in a cascading filter.
/// @param total_count The total number of biquads to be cascaded.
/// @param index 0-index of the biquad to calculate the Q value for.
static double CascadingBiquadQ(size_t total_count, size_t index) {
    const double pole = M_PI * (2 * index + 1) / (4.0 * total_count);
    return 1.0 / (2.0 * std::cos(pole));
}

CascadingFilter CascadingFilter::LowPass(double cutoff, size_t cascade_size) {
    std::vector<Filter> cascade(cascade_size);
    for (size_t i = 0; i < cascade_size; i++) {
        cascade[i] = Filter::LowPass(cutoff, CascadingBiquadQ(cascade_size, i));
    }
    return CascadingFilter{std::move(cascade)};
}

CascadingFilter::CascadingFilter() = default;
CascadingFilter::CascadingFilter(std::vector<Filter> filters) : filters(std::move(filters)) {}

void CascadingFilter::Process(std::vector<s16>& signal) {
    for (auto& filter : filters) {
        filter.Process(signal);
    }
}

} // namespace AudioCore
