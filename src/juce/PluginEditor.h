#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>

class MuteToggleButton final : public juce::ToggleButton {
public:
    MuteToggleButton();
    void paintButton(juce::Graphics& graphics, bool isMouseOver, bool isButtonDown) override;
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

private:
    void timerCallback() override;
    void refreshTuningList();
    void applyCustomTuningFromEditors();
    void setDrawerOpen(bool shouldOpen);
    void updateTuningButtonText();
    static void styleComboBox(juce::ComboBox& comboBox);

    GravePitchAudioProcessor& processor_;
    GravePitchSnapshot currentSnapshot_;
    bool drawerOpen_ = false;
    juce::Rectangle<int> drawerBounds_;
    juce::Rectangle<float> meterBounds_;

    juce::Label noteLabel_;
    juce::Label stringLabel_;
    juce::Label frequencyLabel_;
    juce::Label centsLabel_;
    juce::Label statusLabel_;
    juce::Label levelLabel_;
    MuteToggleButton muteButton_;
    juce::TextButton tuningDrawerButton_;

    juce::Label drawerTitleLabel_;
    juce::ComboBox tuningBox_;
    juce::Slider a4Slider_;
    juce::Label a4Label_;
    juce::TextButton saveCustomButton_;
    juce::TextButton doneButton_;
    std::array<juce::ComboBox, 6> stringEditors_;
    std::array<juce::Label, 6> stringLabels_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GravePitchAudioProcessorEditor)
};
