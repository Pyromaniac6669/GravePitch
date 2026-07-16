#include "gravepitch/core/Note.h"
#include "gravepitch/core/PitchDetector.h"
#include "gravepitch/core/PitchSmoother.h"
#include "gravepitch/core/ReadoutContinuity.h"
#include "gravepitch/core/RealtimeReadout.h"
#include "gravepitch/core/TunerEngine.h"
#include "gravepitch/core/Tuning.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expectTrue(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance) {
        ++failures;
        std::cerr << "FAIL: " << message << " expected " << expected << " got " << actual << '\n';
    }
}

std::vector<float> sineWave(double frequencyHz, double sampleRate, int sampleCount)
{
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    constexpr double pi = 3.14159265358979323846;

    for (int i = 0; i < sampleCount; ++i) {
        samples[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(2.0 * pi * frequencyHz * i / sampleRate));
    }

    return samples;
}

std::vector<float> harmonicRichWave(double frequencyHz, double sampleRate, int sampleCount)
{
    std::vector<float> samples(static_cast<std::size_t>(sampleCount));
    constexpr double pi = 3.14159265358979323846;

    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double value = 0.65 * std::sin(2.0 * pi * frequencyHz * t)
            + 0.25 * std::sin(2.0 * pi * frequencyHz * 2.0 * t)
            + 0.10 * std::sin(2.0 * pi * frequencyHz * 3.0 * t);
        samples[static_cast<std::size_t>(i)] = static_cast<float>(value);
    }

    return samples;
}

double centeredRms(const std::vector<float>& samples)
{
    double mean = 0.0;
    for (const auto sample : samples) {
        mean += sample;
    }
    mean /= static_cast<double>(samples.size());

    double sumSquares = 0.0;
    for (const auto sample : samples) {
        const double centered = sample - mean;
        sumSquares += centered * centered;
    }
    return std::sqrt(sumSquares / static_cast<double>(samples.size()));
}

std::vector<float> withCenteredRms(std::vector<float> samples, double targetRms)
{
    const double currentRms = centeredRms(samples);
    const double scale = currentRms > 0.0 ? targetRms / currentRms : 0.0;
    for (auto& sample : samples) {
        sample = static_cast<float>(sample * scale);
    }
    return samples;
}

void testNoteMath()
{
    using namespace gravepitch;

    expectNear(frequencyForMidiNote(69), 440.0, 0.0001, "A4 is 440Hz");
    expectNear(frequencyForMidiNote(57), 220.0, 0.0001, "A3 is 220Hz");
    expectTrue(nearestMidiNote(441.0) == 69, "441Hz maps to A4");
    expectNear(centsDifference(466.1637615, 69), 100.0, 0.01, "A sharp is 100 cents above A");
    expectTrue(noteNameForMidi(64) == "E4", "MIDI 64 is E4");
    expectTrue(midiNoteFromName("Eb2").value_or(-1) == 39, "Eb2 parses to MIDI 39");
    expectTrue(midiNoteFromName("C#3").value_or(-1) == 49, "C#3 parses to MIDI 49");
}

void testTunings()
{
    using namespace gravepitch;

    const auto standard = tuningById("standard");
    expectTrue(standard.has_value(), "standard tuning exists");
    expectTrue(standard->midiNotesLowToHigh.size() == 6, "standard tuning has six strings");
    expectTrue(standard->midiNotesLowToHigh.front() == midiNoteFromName("E2").value(), "standard low string is E2");
    expectTrue(standard->midiNotesLowToHigh.back() == midiNoteFromName("E4").value(), "standard high string is E4");

    const auto target = standard->nearestTarget(frequencyForMidiNote(midiNoteFromName("A2").value()), 440.0);
    expectTrue(target.has_value(), "A2 finds a target");
    expectTrue(target->stringNumber == 5, "A2 maps to string 5");

    const auto dropC = tuningById("drop_c");
    expectTrue(dropC.has_value(), "Drop C exists");
    expectTrue(dropC->midiNotesLowToHigh.front() == midiNoteFromName("C2").value(), "Drop C low string is C2");

    const auto custom = tuningFromNoteNames("custom", "Custom", {"C2", "G2", "C3", "F3", "A3", "D4"});
    expectTrue(custom.has_value(), "custom tuning parses");
    const auto serialized = serializeCustomTuning(*custom);
    const auto restored = deserializeCustomTuning(serialized);
    expectTrue(restored.has_value(), "custom tuning deserializes");
    expectTrue(restored->midiNotesLowToHigh == custom->midiNotesLowToHigh, "custom tuning round-trips");
}

void testPitchDetector()
{
    using namespace gravepitch;

    PitchDetector detector;
    constexpr double sampleRate = 48000.0;
    constexpr int sampleCount = 4096;

    const auto a2 = sineWave(110.0, sampleRate, sampleCount);
    const auto detectedA2 = detector.process(a2.data(), static_cast<int>(a2.size()), sampleRate);
    expectTrue(detectedA2.frequencyHz.has_value(), "sine A2 is detected");
    expectNear(*detectedA2.frequencyHz, 110.0, 0.5, "sine A2 frequency is accurate");
    expectTrue(detectedA2.confidence > 0.90, "sine A2 confidence is high");

    const auto richE2 = harmonicRichWave(82.4069, sampleRate, sampleCount);
    const auto detectedE2 = detector.process(richE2.data(), static_cast<int>(richE2.size()), sampleRate);
    expectTrue(detectedE2.frequencyHz.has_value(), "harmonic rich E2 is detected");
    expectNear(*detectedE2.frequencyHz, 82.4069, 0.8, "harmonic rich E2 fundamental is detected");

    const auto weakA2 = withCenteredRms(sineWave(110.0, sampleRate, sampleCount), 0.00011);
    const auto detectedWeakA2 = detector.process(
        weakA2.data(),
        static_cast<int>(weakA2.size()),
        sampleRate);
    expectTrue(detectedWeakA2.frequencyHz.has_value(), "weak sine above internal floor is detected");
    if (detectedWeakA2.frequencyHz) {
        expectNear(*detectedWeakA2.frequencyHz, 110.0, 0.5, "weak sine frequency is accurate");
    }

    const auto weakRichE2 = withCenteredRms(harmonicRichWave(82.4069, sampleRate, sampleCount), 0.00011);
    const auto detectedWeakRichE2 = detector.process(
        weakRichE2.data(),
        static_cast<int>(weakRichE2.size()),
        sampleRate);
    expectTrue(detectedWeakRichE2.frequencyHz.has_value(), "weak harmonic signal above internal floor is detected");
    if (detectedWeakRichE2.frequencyHz) {
        expectNear(*detectedWeakRichE2.frequencyHz, 82.4069, 0.8, "weak harmonic fundamental is accurate");
    }

    const auto belowFloorA2 = withCenteredRms(sineWave(110.0, sampleRate, sampleCount), 0.00009);
    const auto rejectedBelowFloor = detector.process(
        belowFloorA2.data(),
        static_cast<int>(belowFloorA2.size()),
        sampleRate);
    expectTrue(!rejectedBelowFloor.frequencyHz.has_value(), "signal below internal floor is rejected");

    const std::vector<float> silence(static_cast<std::size_t>(sampleCount), 0.0f);
    const auto silent = detector.process(silence.data(), static_cast<int>(silence.size()), sampleRate);
    expectTrue(!silent.frequencyHz.has_value(), "silence has no pitch");
}

void testSmootherAndEngine()
{
    using namespace gravepitch;

    PitchSmoother smoother;
    expectTrue(!smoother.push({110.0, 0.95, 0.2}).stable, "first frame is not stable yet");
    expectTrue(!smoother.push({111.0, 0.95, 0.2}).stable, "second frame is not stable yet");
    const auto stable = smoother.push({109.5, 0.95, 0.2});
    expectTrue(stable.stable, "third consistent frame is stable");
    expectNear(*stable.frequencyHz, 110.0, 1.0, "stable median stays near input");

    TunerEngine engine;
    const auto standard = tuningById("standard");
    engine.setTuning(*standard);
    constexpr double sampleRate = 48000.0;
    const auto wave = sineWave(110.0, sampleRate, 4096);

    engine.process(wave.data(), static_cast<int>(wave.size()), sampleRate);
    engine.process(wave.data(), static_cast<int>(wave.size()), sampleRate);
    const auto readout = engine.process(wave.data(), static_cast<int>(wave.size()), sampleRate);

    expectTrue(readout.frequencyHz.has_value(), "engine reports frequency");
    expectTrue(readout.target.has_value(), "engine reports target string");
    expectTrue(readout.target->stringNumber == 5, "engine maps A2 to string 5");
    expectNear(readout.cents, 0.0, 2.0, "engine reports tuned A2 near zero cents");
}

void establishA2TailTracking(gravepitch::PitchSmoother& smoother)
{
    smoother.push({110.0, 0.95, 0.001});
    smoother.push({110.1, 0.95, 0.001});
    const auto established = smoother.push({109.9, 0.95, 0.001});
    expectTrue(established.stable, "three normal frames establish stable pitch");
}

void testLowLevelTailSmoothing()
{
    using namespace gravepitch;

    const double tailRms = std::pow(10.0, -75.0 / 20.0);

    PitchSmoother idle;
    const auto idleWeak = idle.push({110.0, 0.95, tailRms});
    expectTrue(!idleWeak.frequencyHz.has_value(), "weak signal cannot acquire from idle");

    PitchSmoother notStable;
    notStable.push({110.0, 0.95, 0.001});
    notStable.push({110.1, 0.95, 0.001});
    const auto earlyTail = notStable.push({110.0, 0.95, tailRms});
    expectTrue(!earlyTail.frequencyHz.has_value(), "tail tracking requires stable acquisition");

    PitchSmoother tracking;
    establishA2TailTracking(tracking);
    const auto acceptedTail = tracking.push({110.05, 0.95, tailRms});
    expectTrue(acceptedTail.frequencyHz.has_value(), "established pitch accepts minus75dB tail");
    if (acceptedTail.frequencyHz) {
        expectNear(*acceptedTail.frequencyHz, 110.0, 0.2, "accepted tail stays near recent median");
    }
    const auto rejectedOctave = tracking.push({220.0, 0.95, tailRms});
    expectTrue(!rejectedOctave.frequencyHz.has_value(), "weak octave jump is rejected");

    PitchSmoother atBoundary;
    establishA2TailTracking(atBoundary);
    const double insideBoundaryA2 = 110.0 * std::pow(2.0, 49.9 / 1200.0);
    const auto acceptedBoundary = atBoundary.push({insideBoundaryA2, 0.95, tailRms});
    expectTrue(acceptedBoundary.frequencyHz.has_value(), "weak candidate inside 50 cents is accepted");

    PitchSmoother outsideBoundary;
    establishA2TailTracking(outsideBoundary);
    const double outsideBoundaryA2 = 110.0 * std::pow(2.0, 50.1 / 1200.0);
    const auto rejectedOutsideBoundary = outsideBoundary.push({outsideBoundaryA2, 0.95, tailRms});
    expectTrue(!rejectedOutsideBoundary.frequencyHz.has_value(), "weak candidate above 50 cents is rejected");

    PitchSmoother belowTail;
    establishA2TailTracking(belowTail);
    const auto tooQuiet = belowTail.push({110.0, 0.95, tailRms * 0.9});
    expectTrue(!tooQuiet.frequencyHz.has_value(), "signal below tail floor is rejected");

    PitchSmoother oneMiss;
    establishA2TailTracking(oneMiss);
    oneMiss.push({std::nullopt, 0.0, tailRms});
    const auto weakAfterOneMiss = oneMiss.push({110.0, 0.95, tailRms});
    expectTrue(weakAfterOneMiss.frequencyHz.has_value(), "one miss preserves weak tail tracking");

    PitchSmoother expired;
    establishA2TailTracking(expired);
    expired.push({std::nullopt, 0.0, tailRms});
    expired.push({std::nullopt, 0.0, tailRms});
    const auto weakAfterTwoMisses = expired.push({110.0, 0.95, tailRms});
    expectTrue(!weakAfterTwoMisses.frequencyHz.has_value(), "two misses disarm weak tail tracking");
    const auto normalAfterTwoMisses = expired.push({146.832, 0.95, 0.001});
    expectTrue(normalAfterTwoMisses.frequencyHz.has_value(), "normal signal can acquire after tail disarms");

    PitchSmoother resetSmoother;
    establishA2TailTracking(resetSmoother);
    resetSmoother.reset();
    const auto weakAfterReset = resetSmoother.push({110.0, 0.95, tailRms});
    expectTrue(!weakAfterReset.frequencyHz.has_value(), "reset clears weak tail tracking");
}

void testLowLevelTailEngineIntegration()
{
    using namespace gravepitch;

    constexpr double sampleRate = 48000.0;
    constexpr int sampleCount = 4096;
    const double tailRms = std::pow(10.0, -75.0 / 20.0);
    const auto strongA2 = withCenteredRms(sineWave(110.0, sampleRate, sampleCount), 0.001);
    const auto tailA2 = withCenteredRms(sineWave(110.0, sampleRate, sampleCount), tailRms * 1.001);

    TunerEngine engine;
    const auto standard = tuningById("standard");
    expectTrue(standard.has_value(), "standard tuning exists for tail integration");
    if (!standard) {
        return;
    }
    engine.setTuning(*standard);

    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    const auto tailReadout = engine.process(tailA2.data(), sampleCount, sampleRate);
    expectTrue(tailReadout.frequencyHz.has_value(), "engine tracks established minus75dB tail");
    expectTrue(tailReadout.target.has_value(), "tail readout keeps target string");
    if (tailReadout.target) {
        expectTrue(tailReadout.target->stringNumber == 5, "tail readout remains on A2 string");
    }

    engine.setA4Hz(442.0);
    const auto weakAfterA4Change = engine.process(tailA2.data(), sampleCount, sampleRate);
    expectTrue(!weakAfterA4Change.frequencyHz.has_value(), "A4 change clears weak tail tracking");

    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.setTuning(*standard);
    const auto weakAfterTuningChange = engine.process(tailA2.data(), sampleCount, sampleRate);
    expectTrue(!weakAfterTuningChange.frequencyHz.has_value(), "tuning change clears weak tail tracking");

    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.process(strongA2.data(), sampleCount, sampleRate);
    engine.reset();
    const auto idleWeakReadout = engine.process(tailA2.data(), sampleCount, sampleRate);
    expectTrue(!idleWeakReadout.frequencyHz.has_value(), "engine cannot acquire weak pitch after reset");
}

void testRealtimeReadoutRoundTrip()
{
    using namespace gravepitch;

    RealtimeReadout readout;
    RealtimeReadoutData input;
    input.hasPitch = true;
    input.stable = true;
    input.frequencyHz = 110.0;
    input.cents = -2.5;
    input.confidence = 0.93;
    input.rms = 0.2;
    input.stringNumber = 5;
    input.midiNote = 57;
    input.targetMidiNote = 57;
    input.displayPhase = DisplayPhase::fading;
    input.displayOpacity = 0.4;
    readout.publish(input);

    const auto snapshot = readout.load();

    expectTrue(snapshot.hasPitch, "realtime readout stores pitch state");
    expectTrue(snapshot.stable, "realtime readout stores stable state");
    expectNear(snapshot.frequencyHz, 110.0, 0.0001, "realtime readout stores frequency");
    expectNear(snapshot.cents, -2.5, 0.0001, "realtime readout stores cents");
    expectNear(snapshot.confidence, 0.93, 0.0001, "realtime readout stores confidence");
    expectNear(snapshot.rms, 0.2, 0.0001, "realtime readout stores rms");
    expectTrue(snapshot.stringNumber == 5, "realtime readout stores string number");
    expectTrue(snapshot.midiNote == 57, "realtime readout stores note");
    expectTrue(snapshot.targetMidiNote == 57, "realtime readout stores target note");
    expectTrue(snapshot.displayPhase == DisplayPhase::fading, "realtime readout stores display phase");
    expectNear(snapshot.displayOpacity, 0.4, 0.0001, "realtime readout stores display opacity");
}

void testReadoutContinuity()
{
    using namespace gravepitch;

    constexpr double sampleRate = 48000.0;
    ReadoutContinuity continuity;

    TunerReadout a2;
    a2.frequencyHz = 110.0;
    a2.midiNote = 45;
    a2.cents = -1.5;
    a2.confidence = 0.94;
    a2.rms = 0.20;
    a2.stable = true;

    const auto tracking = continuity.update(a2, 0, sampleRate);
    expectTrue(tracking.phase == DisplayPhase::tracking, "valid pitch starts tracking");
    expectNear(tracking.displayOpacity, 1.0, 0.0001, "tracking is fully visible");

    TunerReadout missing;
    missing.rms = 0.01;

    const auto holding = continuity.update(missing, 11999, sampleRate);
    expectTrue(holding.phase == DisplayPhase::holding, "pitch holds before 250ms");
    expectNear(*holding.readout.frequencyHz, 110.0, 0.0001, "holding keeps last pitch");
    expectNear(holding.readout.rms, 0.01, 0.0001, "holding keeps current input level");
    expectTrue(!holding.readout.stable, "held pitch is not stable");

    const auto fadeStart = continuity.update(missing, 12000, sampleRate);
    expectTrue(fadeStart.phase == DisplayPhase::fading, "fade starts at 250ms");
    expectNear(fadeStart.displayOpacity, 1.0, 0.0001, "fade starts fully visible");

    const auto fadeMiddle = continuity.update(missing, 18000, sampleRate);
    expectNear(fadeMiddle.displayOpacity, 0.5, 0.0001, "fade is linear at 375ms");

    TunerReadout d3 = a2;
    d3.frequencyHz = 146.832;
    d3.midiNote = 50;
    const auto replaced = continuity.update(d3, 19024, sampleRate);
    expectTrue(replaced.phase == DisplayPhase::tracking, "new pitch replaces fading pitch");
    expectNear(*replaced.readout.frequencyHz, 146.832, 0.001, "replacement uses new pitch");

    continuity.reset();
    continuity.update(a2, 0, sampleRate);
    const auto idle = continuity.update(missing, 24000, sampleRate);
    expectTrue(idle.phase == DisplayPhase::idle, "pitch clears at 500ms");
    expectTrue(!idle.readout.frequencyHz.has_value(), "idle has no pitch");
    expectNear(idle.readout.rms, 0.01, 0.0001, "idle keeps current input level");
}

} // namespace

int main()
{
    testNoteMath();
    testTunings();
    testPitchDetector();
    testSmootherAndEngine();
    testRealtimeReadoutRoundTrip();
    testLowLevelTailSmoothing();
    testLowLevelTailEngineIntegration();
    testReadoutContinuity();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
