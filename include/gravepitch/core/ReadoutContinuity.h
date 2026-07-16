#pragma once

#include "gravepitch/core/TunerEngine.h"

#include <cstdint>
#include <optional>

namespace gravepitch {

enum class DisplayPhase {
    idle,
    tracking,
    holding,
    fading
};

struct ContinuousReadout {
    TunerReadout readout;
    DisplayPhase phase = DisplayPhase::idle;
    double displayOpacity = 1.0;
};

class ReadoutContinuity {
public:
    void reset();
    ContinuousReadout update(const TunerReadout& current, std::uint64_t windowEndSample, double sampleRate);

private:
    std::optional<TunerReadout> lastValidReadout_;
    std::uint64_t lastValidSample_ = 0;
};

} // namespace gravepitch
