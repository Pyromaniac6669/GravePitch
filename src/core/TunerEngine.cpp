#include "gravepitch/core/TunerEngine.h"

#include "gravepitch/core/Note.h"

#include <algorithm>
#include <utility>

namespace gravepitch {

TunerEngine::TunerEngine()
{
    auto standard = tuningById("standard");
    if (standard) {
        tuning_ = *standard;
    }
}

void TunerEngine::setTuning(Tuning tuning)
{
    tuning_ = std::move(tuning);
    smoother_.reset();
}

void TunerEngine::setA4Hz(double a4Hz)
{
    a4Hz_ = std::clamp(a4Hz, 432.0, 448.0);
    smoother_.reset();
}

void TunerEngine::reset()
{
    smoother_.reset();
}

const Tuning& TunerEngine::tuning() const
{
    return tuning_;
}

double TunerEngine::a4Hz() const
{
    return a4Hz_;
}

TunerReadout TunerEngine::process(const float* samples, int sampleCount, double sampleRate)
{
    const auto frame = detector_.process(samples, sampleCount, sampleRate);
    const auto smoothed = smoother_.push(frame);

    TunerReadout readout;
    readout.confidence = smoothed.confidence;
    readout.rms = smoothed.rms;
    readout.stable = smoothed.stable;

    if (!smoothed.frequencyHz) {
        return readout;
    }

    readout.frequencyHz = smoothed.frequencyHz;
    readout.midiNote = nearestMidiNote(*smoothed.frequencyHz, a4Hz_);
    readout.noteName = noteNameForMidi(*readout.midiNote);
    readout.target = tuning_.nearestTarget(*smoothed.frequencyHz, a4Hz_);

    if (readout.target) {
        readout.cents = centsDifference(*smoothed.frequencyHz, readout.target->midiNote, a4Hz_);
    } else {
        readout.cents = centsDifference(*smoothed.frequencyHz, *readout.midiNote, a4Hz_);
    }

    return readout;
}

} // namespace gravepitch
