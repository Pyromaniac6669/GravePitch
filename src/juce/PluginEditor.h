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

class GravePitchLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void setUiTypefaces(juce::Typeface::Ptr primary, juce::Typeface::Ptr fallback);
    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;

private:
    juce::Font uiFont(float height) const;

    juce::Typeface::Ptr primaryTypeface_;
    juce::Typeface::Ptr fallbackTypeface_;
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
    void drawLanguageSwitch(juce::Graphics& graphics) const;
    void drawDrawerOverlay(juce::Graphics& graphics) const;
    void drawDrawerValues(juce::Graphics& graphics) const;
    void refreshTuningList(bool refreshStringEditors = true);
    void applyCustomTuningFromEditors();
    void setDrawerOpen(bool shouldOpen);
    void syncLanguageFromProcessor();
    void updateLocalizedComponentText();
    void updateTuningButtonText();
    juce::Font uiFont(float height) const;
    juce::String translate(const juce::String& englishText) const;
    juce::String stringLabel(int stringNumber) const;
    juce::String currentTuningName() const;
    static void styleComboBox(juce::ComboBox& comboBox);

    GravePitchAudioProcessor& processor_;
    GravePitchSnapshot currentSnapshot_;
    bool drawerOpen_ = false;
    juce::Rectangle<int> drawerBounds_;
    juce::Typeface::Ptr uiTypeface_;
    juce::Typeface::Ptr cjkTypeface_;
    juce::Image mainPlate_;
    juce::Image brandLogo_;
    GravePitchLookAndFeel drawerLookAndFeel_;
    juce::LocalisedStrings simplifiedChineseStrings_;
    GravePitchUiLanguage uiLanguage_ = GravePitchUiLanguage::english;

    InvisibleTextButton englishLanguageButton_;
    InvisibleTextButton chineseLanguageButton_;
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
