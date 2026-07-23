#pragma once

#include "RealtimePitchAnalyzer.h"
#include "gravepitch/core/RealtimeReadout.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>
#include <vector>

struct GravePitchSnapshot {
    bool hasPitch = false;
    bool stable = false;
    double frequencyHz = 0.0;
    double cents = 0.0;
    double confidence = 0.0;
    double rms = 0.0;
    int stringNumber = 0;
    gravepitch::DisplayPhase displayPhase = gravepitch::DisplayPhase::idle;
    double displayOpacity = 1.0;
    juce::String noteName;
    juce::String targetName;
};

enum class GravePitchUiLanguage {
    english,
    simplifiedChinese
};

class GravePitchAudioProcessor final : public juce::AudioProcessor {
public:
    GravePitchAudioProcessor();
    ~GravePitchAudioProcessor() override = default;

    const juce::String getName() const override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    GravePitchSnapshot snapshot() const;
    std::vector<gravepitch::Tuning> tunings() const;
    int tuningIndex() const;
    double a4Hz() const;
    bool outputMuted() const noexcept;
    GravePitchUiLanguage uiLanguage() const;

    void setTuningIndex(int index);
    void setA4Hz(double value);
    void setCustomTuning(const std::vector<juce::String>& notesLowToHigh);
    void setOutputMuted(bool shouldMute) noexcept;
    void setUiLanguage(GravePitchUiLanguage language);

private:
    void rebuildEngineLocked();

    mutable std::mutex nonAudioStateMutex_;
    std::vector<gravepitch::Tuning> tunings_;
    int selectedTuningIndex_ = 0;
    double a4Hz_ = gravepitch::defaultA4Hz;
    juce::String customTuningState_;
    std::atomic<bool> outputMuted_ {true};
    GravePitchUiLanguage uiLanguage_ = GravePitchUiLanguage::english;

    gravepitch::RealtimeReadout realtimeReadout_;
    RealtimePitchAnalyzer realtimeAnalyzer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GravePitchAudioProcessor)
};
