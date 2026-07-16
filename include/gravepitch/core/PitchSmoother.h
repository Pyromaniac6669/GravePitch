#pragma once

#include "gravepitch/core/PitchDetector.h"

#include <optional>
#include <vector>

namespace gravepitch {

struct SmoothedPitch {
    std::optional<double> frequencyHz;
    double confidence = 0.0;
    double rms = 0.0;
    bool stable = false;
};

class PitchSmoother {
public:
    void reset();
    SmoothedPitch push(const PitchFrame& frame);

private:
    std::vector<double> recentFrequencies_;
    int unstableFrames_ = 0;
    bool tailTrackingEstablished_ = false;
};

} // namespace gravepitch
