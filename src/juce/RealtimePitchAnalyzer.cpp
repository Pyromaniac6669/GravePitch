#include "RealtimePitchAnalyzer.h"

#include <algorithm>
#include <utility>

void gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock::store(
    double a4Hz,
    const gravepitch::Tuning& tuning) noexcept
{
    sequence_.fetch_add(1);
    a4Hz_.store(a4Hz);
    for (std::size_t index = 0; index < midiNotes_.size(); ++index) {
        const int note = index < tuning.midiNotesLowToHigh.size() ? tuning.midiNotesLowToHigh[index] : 0;
        midiNotes_[index].store(note);
#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
        if (index + 1 == midiNotes_.size() / 2 && storeMidpointHook_ != nullptr) {
            storeMidpointHook_(storeMidpointContext_);
        }
#endif
    }
    sequence_.fetch_add(1);
}

bool gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock::loadIfChanged(
    unsigned int& appliedSequence,
    double& a4Hz,
    std::array<int, stringCount>& midiNotes) const noexcept
{
    const auto before = sequence_.load();
    if ((before & 1U) != 0U || before == appliedSequence) {
        return false;
    }

    a4Hz = a4Hz_.load();
    for (std::size_t index = 0; index < midiNotes_.size(); ++index) {
        midiNotes[index] = midiNotes_[index].load();
    }

    const auto after = sequence_.load();
    if (before != after || (after & 1U) != 0U) {
        return false;
    }

    appliedSequence = after;
    return true;
}

unsigned int gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock::sequence() const noexcept
{
    return sequence_.load();
}

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
void gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock::setStoreMidpointHook(
    StoreMidpointHook hook,
    void* context) noexcept
{
    storeMidpointHook_ = hook;
    storeMidpointContext_ = context;
}
#endif

RealtimePitchAnalyzer::RealtimePitchAnalyzer(gravepitch::RealtimeReadout& realtimeReadout)
    : juce::Thread("GravePitch Analysis")
    , realtimeReadout_(realtimeReadout)
{
}

RealtimePitchAnalyzer::~RealtimePitchAnalyzer()
{
    release();
}

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
void RealtimePitchAnalyzer::setWorkerIterationHook(WorkerIterationHook hook, void* context) noexcept
{
    workerIterationHook_ = hook;
    workerIterationContext_ = context;
}
#endif

bool RealtimePitchAnalyzer::prepare(double sampleRate)
{
    release();
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    ringBuffer_.fill(0.0f);
    ringWritePosition_ = 0;
    samplesUntilPublish_ = analysisHopSize;
    totalSamples_ = 0;
    windowExchange_.reset();
    engine_.reset();
    continuity_.reset();
    appliedConfigurationSequence_ = invalidConfigurationSequence;
    publishedConfigurationSequence_.store(invalidConfigurationSequence, std::memory_order_release);
    realtimeReadout_.publish({});

    if (!startThread()) {
        return false;
    }
    acceptingSamples_.store(true, std::memory_order_release);
    return true;
}

void RealtimePitchAnalyzer::release()
{
    acceptingSamples_.store(false, std::memory_order_release);
    if (isThreadRunning()) {
        stopThread(2000);
    }
    windowExchange_.reset();
    continuity_.reset();
    publishedConfigurationSequence_.store(invalidConfigurationSequence, std::memory_order_release);
    realtimeReadout_.publish({});
}

void RealtimePitchAnalyzer::pushMonoSample(float sample) noexcept
{
    if (!acceptingSamples_.load(std::memory_order_acquire)) {
        return;
    }

    ringBuffer_[static_cast<std::size_t>(ringWritePosition_)] = sample;
    ringWritePosition_ = (ringWritePosition_ + 1) % analysisWindowSize;
    ++totalSamples_;

    if (--samplesUntilPublish_ > 0) {
        return;
    }
    samplesUntilPublish_ = analysisHopSize;

    const auto write = windowExchange_.tryBeginWrite();
    if (!write) {
        return;
    }

    const int firstCount = analysisWindowSize - ringWritePosition_;
    std::copy_n(ringBuffer_.data() + ringWritePosition_, firstCount, write->samples);
    std::copy_n(ringBuffer_.data(), ringWritePosition_, write->samples + firstCount);
    windowExchange_.publish(*write, totalSamples_);
}

void RealtimePitchAnalyzer::requestConfiguration(
    double a4Hz,
    const gravepitch::Tuning& tuning) noexcept
{
    requestedConfiguration_.store(a4Hz, tuning);
}

gravepitch::RealtimeReadoutData RealtimePitchAnalyzer::loadCurrentReadout() const noexcept
{
    const auto requestedBefore = requestedConfiguration_.sequence();
    if ((requestedBefore & 1U) != 0U) {
        return {};
    }

    const auto publishedBefore = publishedConfigurationSequence_.load(std::memory_order_acquire);
    if (publishedBefore != requestedBefore) {
        return {};
    }

    const auto data = realtimeReadout_.load();
    const auto requestedAfter = requestedConfiguration_.sequence();
    const auto publishedAfter = publishedConfigurationSequence_.load(std::memory_order_acquire);
    if (requestedBefore != requestedAfter
        || publishedBefore != publishedAfter
        || requestedAfter != publishedAfter
        || (requestedAfter & 1U) != 0U) {
        return {};
    }

    return data;
}

void RealtimePitchAnalyzer::run()
{
    while (!threadShouldExit()) {
#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
        if (workerIterationHook_ != nullptr) {
            workerIterationHook_(workerIterationContext_);
        }
#endif
        if (applyPendingEngineState()) {
            continuity_.reset();
            publishReadout({}, appliedConfigurationSequence_);

            const auto staleWindow = windowExchange_.tryAcquireLatest();
            if (staleWindow) {
                windowExchange_.release(*staleWindow);
            }
            continue;
        }

        const auto window = windowExchange_.tryAcquireLatest();
        if (!window) {
            wait(1);
            continue;
        }

        const auto processingConfigurationSequence = appliedConfigurationSequence_;
        const auto current = engine_.process(window->samples, analysisWindowSize, sampleRate_);
        if (requestedConfiguration_.sequence() != processingConfigurationSequence) {
            windowExchange_.release(*window);
            continue;
        }

        publishReadout(
            continuity_.update(current, window->endSample, sampleRate_),
            processingConfigurationSequence);
        windowExchange_.release(*window);
    }
}

bool RealtimePitchAnalyzer::applyPendingEngineState()
{
    double a4Hz = gravepitch::defaultA4Hz;
    std::array<int, gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock::stringCount> midiNotes {};
    if (!requestedConfiguration_.loadIfChanged(appliedConfigurationSequence_, a4Hz, midiNotes)) {
        return false;
    }

    gravepitch::Tuning tuning;
    tuning.id = "audio";
    tuning.name = "Audio";
    tuning.midiNotesLowToHigh.reserve(midiNotes.size());

    for (const int note : midiNotes) {
        if (note > 0) {
            tuning.midiNotesLowToHigh.push_back(note);
        }
    }

    engine_.setA4Hz(a4Hz);
    if (!tuning.midiNotesLowToHigh.empty()) {
        engine_.setTuning(std::move(tuning));
    }

    return true;
}

void RealtimePitchAnalyzer::publishReadout(
    const gravepitch::ContinuousReadout& result,
    unsigned int configurationSequence) noexcept
{
    const auto& readout = result.readout;
    gravepitch::RealtimeReadoutData next;
    next.hasPitch = readout.frequencyHz.has_value();
    next.stable = readout.stable;
    next.frequencyHz = readout.frequencyHz.value_or(0.0);
    next.cents = readout.cents;
    next.confidence = readout.confidence;
    next.rms = readout.rms;
    next.midiNote = readout.midiNote.value_or(0);
    next.displayPhase = result.phase;
    next.displayOpacity = result.displayOpacity;

    if (readout.target) {
        next.stringNumber = readout.target->stringNumber;
        next.targetMidiNote = readout.target->midiNote;
    }

    realtimeReadout_.publish(next);
    publishedConfigurationSequence_.store(configurationSequence, std::memory_order_release);
}
