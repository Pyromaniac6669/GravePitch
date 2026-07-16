#pragma once

#include "gravepitch/core/Note.h"
#include "gravepitch/core/PitchDetector.h"
#include "gravepitch/core/PitchSmoother.h"
#include "gravepitch/core/Tuning.h"

#include <optional>
#include <string>

namespace gravepitch {

struct TunerReadout {
    std::optional<double> frequencyHz;
    std::optional<int> midiNote;
    std::string noteName;
    std::optional<StringTarget> target;
    double cents = 0.0;
    double confidence = 0.0;
    double rms = 0.0;
    bool stable = false;
};

class TunerEngine {
public:
    TunerEngine();

    void setTuning(Tuning tuning);
    void setA4Hz(double a4Hz);
    void reset();

    const Tuning& tuning() const;
    double a4Hz() const;
    TunerReadout process(const float* samples, int sampleCount, double sampleRate);

private:
    PitchDetector detector_;
    PitchSmoother smoother_;
    Tuning tuning_;
    double a4Hz_ = defaultA4Hz;
};

} // namespace gravepitch
