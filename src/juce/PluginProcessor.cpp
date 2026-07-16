#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "gravepitch/core/Note.h"

#include <algorithm>

GravePitchAudioProcessor::GravePitchAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , tunings_(gravepitch::builtInTunings())
    , realtimeAnalyzer_(realtimeReadout_)
{
    rebuildEngineLocked();
}

const juce::String GravePitchAudioProcessor::getName() const
{
    return "GravePitch";
}

void GravePitchAudioProcessor::prepareToPlay(double sampleRate, int)
{
    realtimeAnalyzer_.prepare(sampleRate);
}

void GravePitchAudioProcessor::releaseResources()
{
    realtimeAnalyzer_.release();
}

bool GravePitchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input.isDisabled()) {
        return false;
    }

    return input == output && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void GravePitchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const int inputChannels = getTotalNumInputChannels();
    const int outputChannels = getTotalNumOutputChannels();

    for (int channel = inputChannels; channel < outputChannels; ++channel) {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    if (inputChannels <= 0 || buffer.getNumSamples() <= 0) {
        if (outputMuted_.load(std::memory_order_relaxed)) {
            buffer.clear();
        }
        return;
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        float mono = 0.0f;
        for (int channel = 0; channel < inputChannels; ++channel) {
            mono += buffer.getReadPointer(channel)[sample];
        }
        mono /= static_cast<float>(inputChannels);

        realtimeAnalyzer_.pushMonoSample(mono);
    }

    if (outputMuted_.load(std::memory_order_relaxed)) {
        buffer.clear();
    }
}

juce::AudioProcessorEditor* GravePitchAudioProcessor::createEditor()
{
    return new GravePitchAudioProcessorEditor(*this);
}

bool GravePitchAudioProcessor::hasEditor() const
{
    return true;
}

double GravePitchAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int GravePitchAudioProcessor::getNumPrograms()
{
    return 1;
}

int GravePitchAudioProcessor::getCurrentProgram()
{
    return 0;
}

void GravePitchAudioProcessor::setCurrentProgram(int)
{
}

const juce::String GravePitchAudioProcessor::getProgramName(int)
{
    return {};
}

void GravePitchAudioProcessor::changeProgramName(int, const juce::String&)
{
}

bool GravePitchAudioProcessor::acceptsMidi() const
{
    return false;
}

bool GravePitchAudioProcessor::producesMidi() const
{
    return false;
}

bool GravePitchAudioProcessor::isMidiEffect() const
{
    return false;
}

void GravePitchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);

    juce::ValueTree state("GravePitchState");
    state.setProperty("a4Hz", a4Hz_, nullptr);
    state.setProperty("selectedTuningIndex", selectedTuningIndex_, nullptr);
    state.setProperty("customTuning", customTuningState_, nullptr);
    state.setProperty("outputMuted", outputMuted_.load(std::memory_order_relaxed), nullptr);

    if (auto xml = state.createXml()) {
        copyXmlToBinary(*xml, destData);
    }
}

void GravePitchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) {
        return;
    }

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType("GravePitchState")) {
        return;
    }

    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);

    a4Hz_ = std::clamp(static_cast<double>(state.getProperty("a4Hz", gravepitch::defaultA4Hz)), 432.0, 448.0);
    customTuningState_ = state.getProperty("customTuning", "").toString();
    outputMuted_.store(static_cast<bool>(state.getProperty("outputMuted", true)), std::memory_order_relaxed);

    if (customTuningState_.isNotEmpty()) {
        const auto custom = gravepitch::deserializeCustomTuning(customTuningState_.toStdString());
        if (custom) {
            if (tunings_.size() == gravepitch::builtInTunings().size()) {
                tunings_.push_back(*custom);
            } else {
                tunings_.back() = *custom;
            }
        }
    }

    const int restoredIndex = static_cast<int>(state.getProperty("selectedTuningIndex", 0));
    selectedTuningIndex_ = std::clamp(restoredIndex, 0, static_cast<int>(tunings_.size()) - 1);
    rebuildEngineLocked();
}

GravePitchSnapshot GravePitchAudioProcessor::snapshot() const
{
    const auto data = realtimeAnalyzer_.loadCurrentReadout();

    GravePitchSnapshot snapshot;
    snapshot.hasPitch = data.hasPitch;
    snapshot.stable = data.stable;
    snapshot.frequencyHz = data.frequencyHz;
    snapshot.cents = data.cents;
    snapshot.confidence = data.confidence;
    snapshot.rms = data.rms;
    snapshot.stringNumber = data.stringNumber;
    snapshot.displayPhase = data.displayPhase;
    snapshot.displayOpacity = data.displayOpacity;

    if (data.midiNote > 0) {
        snapshot.noteName = gravepitch::noteNameForMidi(data.midiNote);
    }

    if (data.targetMidiNote > 0) {
        snapshot.targetName = gravepitch::noteNameForMidi(data.targetMidiNote);
    }

    return snapshot;
}

std::vector<gravepitch::Tuning> GravePitchAudioProcessor::tunings() const
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    return tunings_;
}

int GravePitchAudioProcessor::tuningIndex() const
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    return selectedTuningIndex_;
}

double GravePitchAudioProcessor::a4Hz() const
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    return a4Hz_;
}

bool GravePitchAudioProcessor::outputMuted() const noexcept
{
    return outputMuted_.load(std::memory_order_relaxed);
}

void GravePitchAudioProcessor::setTuningIndex(int index)
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    selectedTuningIndex_ = std::clamp(index, 0, static_cast<int>(tunings_.size()) - 1);
    rebuildEngineLocked();
}

void GravePitchAudioProcessor::setA4Hz(double value)
{
    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    a4Hz_ = std::clamp(value, 432.0, 448.0);
    rebuildEngineLocked();
}

void GravePitchAudioProcessor::setCustomTuning(const std::vector<juce::String>& notesLowToHigh)
{
    std::vector<std::string> notes;
    notes.reserve(notesLowToHigh.size());

    for (const auto& note : notesLowToHigh) {
        notes.push_back(note.toStdString());
    }

    auto custom = gravepitch::tuningFromNoteNames("custom", "Custom", notes);
    if (!custom) {
        return;
    }

    std::lock_guard<std::mutex> lock(nonAudioStateMutex_);
    const auto builtIns = gravepitch::builtInTunings();

    if (tunings_.size() == builtIns.size()) {
        tunings_.push_back(*custom);
    } else {
        tunings_.back() = *custom;
    }

    customTuningState_ = gravepitch::serializeCustomTuning(*custom);
    selectedTuningIndex_ = static_cast<int>(tunings_.size()) - 1;
    rebuildEngineLocked();
}

void GravePitchAudioProcessor::setOutputMuted(bool shouldMute) noexcept
{
    outputMuted_.store(shouldMute, std::memory_order_relaxed);
}

void GravePitchAudioProcessor::rebuildEngineLocked()
{
    if (tunings_.empty()) {
        tunings_ = gravepitch::builtInTunings();
    }

    selectedTuningIndex_ = std::clamp(selectedTuningIndex_, 0, static_cast<int>(tunings_.size()) - 1);
    realtimeAnalyzer_.requestConfiguration(
        a4Hz_,
        tunings_[static_cast<std::size_t>(selectedTuningIndex_)]);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GravePitchAudioProcessor();
}
