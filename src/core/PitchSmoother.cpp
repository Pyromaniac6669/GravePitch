#include "gravepitch/core/PitchSmoother.h"

#include <algorithm>
#include <cmath>

namespace gravepitch {
namespace {

constexpr double minimumConfidence = 0.75;
constexpr double acquisitionRms = 0.0008;
constexpr double tailRms = 0.00017782794100389227;
constexpr double maximumTailCents = 50.0;

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double centsBetween(double frequencyHz, double referenceHz)
{
    return 1200.0 * std::log2(frequencyHz / referenceHz);
}

} // namespace

void PitchSmoother::reset()
{
    recentFrequencies_.clear();
    unstableFrames_ = 0;
    tailTrackingEstablished_ = false;
}

SmoothedPitch PitchSmoother::push(const PitchFrame& frame)
{
    SmoothedPitch result;
    result.confidence = frame.confidence;
    result.rms = frame.rms;

    const auto rejectFrame = [&]() {
        ++unstableFrames_;
        if (unstableFrames_ >= 2) {
            recentFrequencies_.clear();
            tailTrackingEstablished_ = false;
        }
        return result;
    };

    if (!frame.frequencyHz || frame.confidence < minimumConfidence) {
        return rejectFrame();
    }

    const bool normalLevel = frame.rms >= acquisitionRms;
    if (!normalLevel) {
        if (frame.rms < tailRms
            || !tailTrackingEstablished_
            || recentFrequencies_.empty()) {
            return rejectFrame();
        }

        const double referenceHz = median(recentFrequencies_);
        if (std::abs(centsBetween(*frame.frequencyHz, referenceHz)) > maximumTailCents) {
            return rejectFrame();
        }
    }

    unstableFrames_ = 0;
    recentFrequencies_.push_back(*frame.frequencyHz);

    if (recentFrequencies_.size() > 5) {
        recentFrequencies_.erase(recentFrequencies_.begin());
    }

    result.frequencyHz = median(recentFrequencies_);
    result.stable = recentFrequencies_.size() >= 3;

    if (result.stable) {
        const double center = *result.frequencyHz;
        const auto maxDeviation = std::max_element(
            recentFrequencies_.begin(),
            recentFrequencies_.end(),
            [center](double left, double right) {
                return std::abs(left - center) < std::abs(right - center);
            });

        result.stable = maxDeviation != recentFrequencies_.end()
            && std::abs(*maxDeviation - center) < center * 0.015;
    }

    if (normalLevel && result.stable) {
        tailTrackingEstablished_ = true;
    }

    return result;
}

} // namespace gravepitch
