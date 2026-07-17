#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace {

bool expectTrue(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }

    return true;
}

bool expectNear(float actual, float expected, const char* message)
{
    return expectTrue(std::abs(actual - expected) < 0.000001f, message);
}

bool bufferIsSilent(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            if (buffer.getSample(channel, sample) != 0.0f) {
                return false;
            }
        }
    }

    return true;
}

void fillStereoSine(
    juce::AudioBuffer<float>& buffer,
    double& phase,
    double frequencyHz,
    double sampleRate)
{
    const double phaseStep = juce::MathConstants<double>::twoPi * frequencyHz / sampleRate;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const float value = static_cast<float>(0.4 * std::sin(phase));
        phase += phaseStep;

        if (phase >= juce::MathConstants<double>::twoPi) {
            phase -= juce::MathConstants<double>::twoPi;
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            buffer.setSample(channel, sample, value);
        }
    }
}

bool testMuteDefaultsToOnAndStillAnalyses()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    GravePitchAudioProcessor processor;
    processor.prepareToPlay(sampleRate, blockSize);

    bool ok = expectTrue(processor.outputMuted(), "output mute defaults to on");
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;

    for (int block = 0; block < 48; ++block) {
        fillStereoSine(buffer, phase, 110.0, sampleRate);
        processor.processBlock(buffer, midi);
        ok &= expectTrue(bufferIsSilent(buffer), "muted processing clears output");
        juce::Thread::sleep(1);
    }

    bool detected = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto snapshot = processor.snapshot();
        if (snapshot.hasPitch && snapshot.noteName == "A2") {
            detected = true;
            break;
        }
        juce::Thread::sleep(5);
    }

    ok &= expectTrue(detected, "muted processing still feeds the pitch analyser");
    processor.releaseResources();
    return ok;
}

bool testUnmutedAudioPassesThrough()
{
    GravePitchAudioProcessor processor;
    processor.setOutputMuted(false);

    juce::AudioBuffer<float> buffer(2, 32);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample(0, 0, 0.25f);
    buffer.setSample(1, 0, -0.5f);

    processor.processBlock(buffer, midi);

    return expectNear(buffer.getSample(0, 0), 0.25f, "unmuted left input passes through")
        && expectNear(buffer.getSample(1, 0), -0.5f, "unmuted right input passes through");
}

bool testMuteStateRoundTrips()
{
    GravePitchAudioProcessor source;
    source.setOutputMuted(false);

    juce::MemoryBlock state;
    source.getStateInformation(state);

    GravePitchAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    return expectTrue(!restored.outputMuted(), "output mute state round-trips");
}

bool testLegacyStateDefaultsToMuted()
{
    juce::ValueTree legacyState("GravePitchState");
    legacyState.setProperty("a4Hz", 440.0, nullptr);
    legacyState.setProperty("selectedTuningIndex", 0, nullptr);
    legacyState.setProperty("customTuning", "", nullptr);

    juce::MemoryBlock state;
    if (auto xml = legacyState.createXml()) {
        juce::AudioProcessor::copyXmlToBinary(*xml, state);
    }

    GravePitchAudioProcessor restored;
    restored.setOutputMuted(false);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    return expectTrue(restored.outputMuted(), "legacy state defaults output mute to on");
}

bool testEditorRendersAtFixedSize()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created");
    if (editor == nullptr) {
        return false;
    }

    ok &= expectTrue(editor->getWidth() == 760 && editor->getHeight() == 460, "editor keeps the fixed design size");

    juce::Image image(juce::Image::ARGB, 760, 460, true);
    juce::Graphics graphics(image);
    editor->paintEntireComponent(graphics, true);
    ok &= expectTrue(image.getPixelAt(4, 4).getAlpha() > 0, "editor paints an opaque background");
    return ok;
}

bool testEditorKeepsOnlyActionableReadouts()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created for readout inspection");
    if (editor == nullptr) {
        return false;
    }

    bool foundInTuneIndicator = false;
    bool foundA4Slider = false;

    for (int index = 0; index < editor->getNumChildComponents(); ++index) {
        auto* component = editor->getChildComponent(index);

        if (component->getComponentID() == "inTuneIndicator") {
            foundInTuneIndicator = true;
        }

        if (auto* label = dynamic_cast<juce::Label*>(component)) {
            const auto text = label->getText();
            ok &= expectTrue(!text.contains("INPUT"), "input dBFS readout is removed");
            ok &= expectTrue(!text.contains("cents"), "numeric cents readout is removed");
            ok &= expectTrue(!text.endsWith("Hz"), "numeric frequency readout is removed");
            ok &= expectTrue(text != "NO SIGNAL", "text status is replaced by the in-tune indicator");
        }

        if (auto* slider = dynamic_cast<juce::Slider*>(component)) {
            foundA4Slider = true;
            ok &= expectTrue(slider->isDoubleClickReturnEnabled(), "A4 slider enables double-click reset");
            ok &= expectTrue(std::abs(slider->getDoubleClickReturnValue() - 440.0) < 0.000001,
                "A4 slider double-click reset returns to 440 Hz");
        }
    }

    ok &= expectTrue(foundInTuneIndicator, "editor contains the in-tune indicator");
    ok &= expectTrue(foundA4Slider, "editor contains the A4 calibration slider");
    return ok;
}

bool testInTuneIndicatorState()
{
    InTuneIndicator indicator;
    bool ok = expectTrue(!indicator.isActive(), "in-tune indicator defaults to off");
    indicator.setActive(true);
    ok &= expectTrue(indicator.isActive(), "in-tune indicator can be switched on");
    indicator.setActive(false);
    ok &= expectTrue(!indicator.isActive(), "in-tune indicator can be switched off");

    GravePitchSnapshot snapshot;
    snapshot.hasPitch = true;
    snapshot.stable = true;
    snapshot.cents = -5.0;
    ok &= expectTrue(InTuneIndicator::shouldBeActiveFor(snapshot), "minus five cents lights the indicator");
    snapshot.cents = 5.0;
    ok &= expectTrue(InTuneIndicator::shouldBeActiveFor(snapshot), "plus five cents lights the indicator");
    snapshot.cents = 5.1;
    ok &= expectTrue(!InTuneIndicator::shouldBeActiveFor(snapshot), "more than five cents keeps the indicator off");
    snapshot.cents = 0.0;
    snapshot.stable = false;
    ok &= expectTrue(!InTuneIndicator::shouldBeActiveFor(snapshot), "unstable pitch keeps the indicator off");
    snapshot.stable = true;
    snapshot.hasPitch = false;
    ok &= expectTrue(!InTuneIndicator::shouldBeActiveFor(snapshot), "missing pitch keeps the indicator off");
    return ok;
}

bool renderEditorReference(const char* collapsedPath, const char* expandedPath)
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr) {
        return false;
    }

    auto writeImage = [editorPointer = editor.get()](const char* path) {
        juce::Image image(juce::Image::ARGB, editorPointer->getWidth(), editorPointer->getHeight(), true);
        juce::Graphics graphics(image);
        editorPointer->paintEntireComponent(graphics, true);

        auto stream = juce::File(path).createOutputStream();
        return stream != nullptr && juce::PNGImageFormat().writeImageToStream(image, *stream);
    };

    if (!writeImage(collapsedPath)) {
        return false;
    }

    for (int index = 0; index < editor->getNumChildComponents(); ++index) {
        if (auto* button = dynamic_cast<juce::TextButton*>(editor->getChildComponent(index))) {
            if (button->getButtonText().contains("6 STRING") && button->onClick != nullptr) {
                button->onClick();
                break;
            }
        }
    }

    return writeImage(expandedPath);
}

} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    bool ok = true;
    ok &= testMuteDefaultsToOnAndStillAnalyses();
    ok &= testUnmutedAudioPassesThrough();
    ok &= testMuteStateRoundTrips();
    ok &= testLegacyStateDefaultsToMuted();
    ok &= testEditorRendersAtFixedSize();
    ok &= testEditorKeepsOnlyActionableReadouts();
    ok &= testInTuneIndicatorState();

    if (ok && argc == 3) {
        ok &= expectTrue(renderEditorReference(argv[1], argv[2]), "editor reference images are written");
    }

    if (!ok) {
        return 1;
    }

    std::cout << "Plugin processor tests passed\n";
    return 0;
}
