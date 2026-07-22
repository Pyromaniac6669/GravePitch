#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>

class MuteToggleButton final : public juce::ToggleButton {
public:
    MuteToggleButton();
    void paintButton(juce::Graphics& graphics, bool isMouseOver, bool isButtonDown) override;
};

class InTuneIndicator final : public juce::Component {
public:
    InTuneIndicator();
    void paint(juce::Graphics& graphics) override;
    void setActive(bool shouldBeActive);
    bool isActive() const noexcept;
    static bool shouldBeActiveFor(const GravePitchSnapshot& snapshot) noexcept;

private:
    bool active_ = false;
};

class InvisibleTextButton final : public juce::TextButton {
public:
    void paintButton(juce::Graphics&, bool, bool) override { }
};

class InvisibleComboBox final : public juce::ComboBox {
public:
    void paint(juce::Graphics&) override { }
};

class GravePitchAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer {
public:
    explicit GravePitchAudioProcessorEditor(GravePitchAudioProcessor& audioProcessor);
    ~GravePitchAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    static float scalePositionForCents(double cents) noexcept;
    static juce::String noteNameWithoutOctave(juce::String noteName);

private:
    void timerCallback() override;
    void drawPitchReadout(juce::Graphics& graphics) const;
    void drawTuningScale(juce::Graphics& graphics) const;
    void drawMovingIndicator(juce::Graphics& graphics) const;
    void drawDrawerOverlay(juce::Graphics& graphics) const;
    void drawDrawerValues(juce::Graphics& graphics) const;
    void refreshTuningList();
    void applyCustomTuningFromEditors();
    void setDrawerOpen(bool shouldOpen);
    void updateTuningButtonText();
    juce::Font uiFont(float height) const;
    juce::String currentTuningName() const;
    static void styleComboBox(juce::ComboBox& comboBox);

    GravePitchAudioProcessor& processor_;
    GravePitchSnapshot currentSnapshot_;
    bool drawerOpen_ = false;
    juce::Rectangle<int> drawerBounds_;
    juce::Typeface::Ptr uiTypeface_;
    juce::Image mainPlate_;
    juce::Image brandLogo_;
    juce::LookAndFeel_V4 drawerLookAndFeel_;

    MuteToggleButton muteButton_;
    InTuneIndicator inTuneIndicator_;
    InvisibleTextButton tuningDrawerButton_;

    InvisibleComboBox tuningBox_;
    juce::Slider a4Slider_;
    InvisibleTextButton saveCustomButton_;
    InvisibleTextButton doneButton_;
    std::array<InvisibleComboBox, 6> stringEditors_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GravePitchAudioProcessorEditor)
};
