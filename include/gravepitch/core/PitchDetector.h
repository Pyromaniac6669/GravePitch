#pragma once

#include <optional>
#include <vector>

namespace gravepitch {

struct PitchFrame {
    std::optional<double> frequencyHz;
    double confidence = 0.0;
    double rms = 0.0;
};

struct PitchDetectorConfig {
    double minFrequencyHz = 40.0;
    double maxFrequencyHz = 1200.0;
    double clarityThreshold = 0.82;
    double silenceRms = 0.0001;
};

class PitchDetector {
public:
    explicit PitchDetector(PitchDetectorConfig config = {});

    PitchFrame process(const float* samples, int sampleCount, double sampleRate);

private:
    PitchDetectorConfig config_;
    std::vector<double> nsdf_;
};

} // namespace gravepitch

