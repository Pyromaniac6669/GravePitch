#include "gravepitch/core/PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gravepitch {
namespace {

double parabolicInterpolation(const std::vector<double>& values, int index)
{
    if (index <= 0 || index + 1 >= static_cast<int>(values.size())) {
        return static_cast<double>(index);
    }

    const double left = values[static_cast<std::size_t>(index - 1)];
    const double center = values[static_cast<std::size_t>(index)];
    const double right = values[static_cast<std::size_t>(index + 1)];
    const double denominator = left - 2.0 * center + right;

    if (std::abs(denominator) < std::numeric_limits<double>::epsilon()) {
        return static_cast<double>(index);
    }

    return static_cast<double>(index) + 0.5 * (left - right) / denominator;
}

} // namespace

PitchDetector::PitchDetector(PitchDetectorConfig config)
    : config_(config)
{
}

PitchFrame PitchDetector::process(const float* samples, int sampleCount, double sampleRate)
{
    PitchFrame frame;

    if (samples == nullptr || sampleCount < 32 || sampleRate <= 0.0) {
        return frame;
    }

    double sumSquares = 0.0;
    double mean = 0.0;

    for (int i = 0; i < sampleCount; ++i) {
        mean += samples[i];
    }
    mean /= sampleCount;

    for (int i = 0; i < sampleCount; ++i) {
        const double centered = samples[i] - mean;
        sumSquares += centered * centered;
    }

    frame.rms = std::sqrt(sumSquares / sampleCount);
    if (frame.rms < config_.silenceRms) {
        return frame;
    }

    const int minLag = std::max(2, static_cast<int>(std::floor(sampleRate / config_.maxFrequencyHz)));
    const int maxLag = std::min(sampleCount - 2, static_cast<int>(std::ceil(sampleRate / config_.minFrequencyHz)));

    if (minLag >= maxLag) {
        return frame;
    }

    nsdf_.assign(static_cast<std::size_t>(maxLag + 1), 0.0);

    for (int tau = minLag; tau <= maxLag; ++tau) {
        double acf = 0.0;
        double divisor = 0.0;

        for (int i = 0; i + tau < sampleCount; ++i) {
            const double x = samples[i] - mean;
            const double y = samples[i + tau] - mean;
            acf += x * y;
            divisor += x * x + y * y;
        }

        nsdf_[static_cast<std::size_t>(tau)] = divisor > 0.0 ? (2.0 * acf / divisor) : 0.0;
    }

    int bestLag = -1;
    double bestValue = -1.0;
    bool passedNegativeRegion = false;

    for (int tau = minLag + 1; tau < maxLag; ++tau) {
        const double current = nsdf_[static_cast<std::size_t>(tau)];

        if (current < 0.0) {
            passedNegativeRegion = true;
        }

        const bool localMaximum = current > nsdf_[static_cast<std::size_t>(tau - 1)]
            && current >= nsdf_[static_cast<std::size_t>(tau + 1)];

        if (!localMaximum) {
            continue;
        }

        if (passedNegativeRegion && current >= config_.clarityThreshold) {
            bestLag = tau;
            bestValue = current;
            break;
        }

        if (current > bestValue) {
            bestLag = tau;
            bestValue = current;
        }
    }

    if (bestLag <= 0 || bestValue < config_.clarityThreshold) {
        frame.confidence = std::max(0.0, bestValue);
        return frame;
    }

    const double refinedLag = parabolicInterpolation(nsdf_, bestLag);
    if (refinedLag <= 0.0) {
        return frame;
    }

    frame.frequencyHz = sampleRate / refinedLag;
    frame.confidence = std::clamp(bestValue, 0.0, 1.0);
    return frame;
}

} // namespace gravepitch

