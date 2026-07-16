#pragma once

#include "LatestAnalysisWindowExchange.h"
#include "gravepitch/core/ReadoutContinuity.h"
#include "gravepitch/core/RealtimeReadout.h"
#include "gravepitch/core/TunerEngine.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gravepitch::realtime_analyzer_detail {

class AnalysisConfigurationSeqlock final {
public:
    static constexpr std::size_t stringCount = 6;

    // 只允许单写者调用。
    void store(double a4Hz, const gravepitch::Tuning& tuning) noexcept;
    bool loadIfChanged(
        unsigned int& appliedSequence,
        double& a4Hz,
        std::array<int, stringCount>& midiNotes) const noexcept;
    unsigned int sequence() const noexcept;

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
    using StoreMidpointHook = void (*)(void*) noexcept;
    void setStoreMidpointHook(StoreMidpointHook hook, void* context) noexcept;
#endif

private:
    std::atomic<double> a4Hz_ {gravepitch::defaultA4Hz};
    std::array<std::atomic<int>, stringCount> midiNotes_ {};
    std::atomic<unsigned int> sequence_ {0};

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
    StoreMidpointHook storeMidpointHook_ = nullptr;
    void* storeMidpointContext_ = nullptr;
#endif
};

}  // namespace gravepitch::realtime_analyzer_detail

class RealtimePitchAnalyzer final : private juce::Thread {
public:
    explicit RealtimePitchAnalyzer(gravepitch::RealtimeReadout& realtimeReadout);
    ~RealtimePitchAnalyzer() override;

    // 调用方必须保证 prepare()/release() 不与 pushMonoSample() 并发；
    // 这与 JUCE AudioProcessor 的 prepareToPlay()/releaseResources()/processBlock() 契约一致。
    bool prepare(double sampleRate);
    void release();
    void pushMonoSample(float sample) noexcept;
    void requestConfiguration(double a4Hz, const gravepitch::Tuning& tuning) noexcept;
    gravepitch::RealtimeReadoutData loadCurrentReadout() const noexcept;

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
    using WorkerIterationHook = void (*)(void*) noexcept;
    void setWorkerIterationHook(WorkerIterationHook hook, void* context) noexcept;
#endif

private:
    void run() override;
    bool applyPendingEngineState();
    void publishReadout(
        const gravepitch::ContinuousReadout& result,
        unsigned int configurationSequence) noexcept;

    static constexpr int analysisWindowSize = gravepitch::LatestAnalysisWindowExchange::windowSize;
    static constexpr int analysisHopSize = 1024;
    static constexpr unsigned int invalidConfigurationSequence = static_cast<unsigned int>(-1);

    gravepitch::RealtimeReadout& realtimeReadout_;
    gravepitch::LatestAnalysisWindowExchange windowExchange_;
    gravepitch::TunerEngine engine_;
    gravepitch::ReadoutContinuity continuity_;

    std::array<float, analysisWindowSize> ringBuffer_ {};
    int ringWritePosition_ = 0;
    int samplesUntilPublish_ = analysisHopSize;
    std::uint64_t totalSamples_ = 0;
    double sampleRate_ = 48000.0;
    std::atomic<bool> acceptingSamples_ {false};

    gravepitch::realtime_analyzer_detail::AnalysisConfigurationSeqlock requestedConfiguration_;
    unsigned int appliedConfigurationSequence_ = 0;
    std::atomic<unsigned int> publishedConfigurationSequence_ {invalidConfigurationSequence};

#if defined(GRAVEPITCH_ENABLE_TEST_SEAMS)
    WorkerIterationHook workerIterationHook_ = nullptr;
    void* workerIterationContext_ = nullptr;
#endif
};
