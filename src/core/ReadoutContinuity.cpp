#include "gravepitch/core/ReadoutContinuity.h"

#include <algorithm>

namespace gravepitch {
namespace {

ContinuousReadout idleReadout(const TunerReadout& current)
{
    TunerReadout idle;
    idle.confidence = current.confidence;
    idle.rms = current.rms;
    return {idle, DisplayPhase::idle, 1.0};
}

} // namespace

void ReadoutContinuity::reset()
{
    lastValidReadout_.reset();
    lastValidSample_ = 0;
}

ContinuousReadout ReadoutContinuity::update(
    const TunerReadout& current,
    std::uint64_t windowEndSample,
    double sampleRate)
{
    if (current.frequencyHz.has_value()) {
        lastValidReadout_ = current;
        lastValidSample_ = windowEndSample;
        return {current, DisplayPhase::tracking, 1.0};
    }

    if (!lastValidReadout_.has_value() || sampleRate <= 0.0) {
        return idleReadout(current);
    }

    const auto elapsedSamples = windowEndSample >= lastValidSample_
        ? windowEndSample - lastValidSample_
        : 0;
    const double elapsedSeconds = static_cast<double>(elapsedSamples) / sampleRate;

    if (elapsedSeconds >= 0.5) {
        lastValidReadout_.reset();
        return idleReadout(current);
    }

    auto held = *lastValidReadout_;
    held.confidence = current.confidence;
    held.rms = current.rms;
    held.stable = false;

    if (elapsedSeconds < 0.25) {
        return {held, DisplayPhase::holding, 1.0};
    }

    const double opacity = std::clamp(1.0 - (elapsedSeconds - 0.25) / 0.25, 0.0, 1.0);
    return {held, DisplayPhase::fading, opacity};
}

} // namespace gravepitch
