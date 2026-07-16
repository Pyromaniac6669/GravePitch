#include "PluginEditor.h"

#include "gravepitch/core/Note.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

const auto backgroundColour = juce::Colour(0xff0d0f10);
const auto panelColour = juce::Colour(0xff171a1c);
const auto raisedPanelColour = juce::Colour(0xff202326);
const auto borderColour = juce::Colour(0xff5d5140);
const auto accentColour = juce::Colour(0xffd1a457);
const auto textColour = juce::Colour(0xffe2dfd8);
const auto secondaryTextColour = juce::Colour(0xff9d9b96);
const auto inTuneColour = juce::Colour(0xff7d9276);

juce::String formatFrequency(double frequencyHz)
{
    return frequencyHz > 0.0 ? juce::String(frequencyHz, 2) + " Hz" : "-- Hz";
}

juce::String formatCents(double cents)
{
    if (cents < -50.0) {
        return "< -50 cents";
    }
    if (cents > 50.0) {
        return "> +50 cents";
    }

    const auto sign = cents > 0.0 ? "+" : "";
    return juce::String(sign) + juce::String(cents, 1) + " cents";
}

juce::String statusText(const GravePitchSnapshot& snapshot)
{
    if (!snapshot.hasPitch) {
        return "NO SIGNAL";
    }
    if (!snapshot.stable) {
        return "LISTENING";
    }
    if (std::abs(snapshot.cents) <= 5.0) {
        return "IN TUNE";
    }
    return snapshot.cents < 0.0 ? "FLAT" : "SHARP";
}

std::vector<juce::String> editableNoteNames()
{
    return {
        "C1", "C#1", "Db1", "D1", "D#1", "Eb1", "E1", "F1", "F#1", "Gb1", "G1", "G#1", "Ab1", "A1", "A#1", "Bb1", "B1",
        "C2", "C#2", "Db2", "D2", "D#2", "Eb2", "E2", "F2", "F#2", "Gb2", "G2", "G#2", "Ab2", "A2", "A#2", "Bb2", "B2",
        "C3", "C#3", "Db3", "D3", "D#3", "Eb3", "E3", "F3", "F#3", "Gb3", "G3", "G#3", "Ab3", "A3", "A#3", "Bb3", "B3",
        "C4", "C#4", "Db4", "D4", "D#4", "Eb4", "E4", "F4", "F#4", "Gb4", "G4", "G#4", "Ab4", "A4", "A#4", "Bb4", "B4",
        "C5", "C#5", "Db5", "D5", "D#5", "Eb5", "E5"
    };
}

float centsToX(const juce::Rectangle<float>& scaleBounds, double cents)
{
    const auto limited = static_cast<float>(std::clamp(cents, -50.0, 50.0));
    return juce::jmap(limited, -50.0f, 50.0f, scaleBounds.getX(), scaleBounds.getRight());
}

} // namespace

MuteToggleButton::MuteToggleButton()
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MuteToggleButton::paintButton(juce::Graphics& graphics, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat();
    auto labelBounds = bounds.removeFromLeft(48.0f);
    auto trackBounds = bounds.reduced(2.0f, 4.0f);
    const bool isOn = getToggleState();

    graphics.setColour(isOn ? accentColour : secondaryTextColour);
    graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    graphics.drawFittedText("MUTE", labelBounds.toNearestInt(), juce::Justification::centredLeft, 1);

    graphics.setColour(isOn ? accentColour.withAlpha(isButtonDown ? 0.65f : 0.42f) : raisedPanelColour);
    graphics.fillRoundedRectangle(trackBounds, trackBounds.getHeight() * 0.5f);
    graphics.setColour((isMouseOver ? accentColour : borderColour).withAlpha(isMouseOver ? 1.0f : 0.8f));
    graphics.drawRoundedRectangle(trackBounds, trackBounds.getHeight() * 0.5f, 1.2f);

    const float thumbSize = trackBounds.getHeight() - 6.0f;
    const float thumbX = isOn
        ? trackBounds.getRight() - thumbSize - 3.0f
        : trackBounds.getX() + 3.0f;
    const juce::Rectangle<float> thumbBounds(thumbX, trackBounds.getY() + 3.0f, thumbSize, thumbSize);

    graphics.setColour(isOn ? textColour : secondaryTextColour);
    graphics.fillEllipse(thumbBounds);
    graphics.setColour(backgroundColour.withAlpha(0.45f));
    graphics.drawEllipse(thumbBounds.reduced(1.0f), 1.0f);

    auto stateBounds = trackBounds.reduced(7.0f, 0.0f).toNearestInt();
    stateBounds = isOn ? stateBounds.withTrimmedRight(static_cast<int>(thumbSize))
                       : stateBounds.withTrimmedLeft(static_cast<int>(thumbSize));
    graphics.setColour(isOn ? textColour : secondaryTextColour);
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawFittedText(isOn ? "ON" : "OFF", stateBounds, juce::Justification::centred, 1);
}

GravePitchAudioProcessorEditor::GravePitchAudioProcessorEditor(GravePitchAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor)
    , processor_(audioProcessor)
{
    setOpaque(true);
    setSize(760, 460);

    auto configureLabel = [this](juce::Label& label, float size, int style, juce::Justification justification) {
        label.setColour(juce::Label::textColourId, textColour);
        label.setFont(juce::FontOptions(size, style));
        label.setJustificationType(justification);
        addAndMakeVisible(label);
    };

    configureLabel(noteLabel_, 96.0f, juce::Font::bold, juce::Justification::centred);
    configureLabel(stringLabel_, 20.0f, juce::Font::plain, juce::Justification::centred);
    stringLabel_.setColour(juce::Label::textColourId, secondaryTextColour);
    configureLabel(frequencyLabel_, 18.0f, juce::Font::plain, juce::Justification::centredLeft);
    configureLabel(centsLabel_, 18.0f, juce::Font::bold, juce::Justification::centredRight);
    configureLabel(statusLabel_, 13.0f, juce::Font::bold, juce::Justification::centred);
    configureLabel(levelLabel_, 13.0f, juce::Font::plain, juce::Justification::centred);
    levelLabel_.setColour(juce::Label::textColourId, secondaryTextColour);

    muteButton_.setToggleState(processor_.outputMuted(), juce::dontSendNotification);
    muteButton_.onClick = [this] {
        processor_.setOutputMuted(muteButton_.getToggleState());
    };
    addAndMakeVisible(muteButton_);

    tuningDrawerButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    tuningDrawerButton_.setColour(juce::TextButton::buttonColourId, panelColour);
    tuningDrawerButton_.setColour(juce::TextButton::buttonOnColourId, raisedPanelColour);
    tuningDrawerButton_.setColour(juce::TextButton::textColourOffId, accentColour);
    tuningDrawerButton_.setColour(juce::TextButton::textColourOnId, accentColour);
    tuningDrawerButton_.onClick = [this] { setDrawerOpen(true); };
    addAndMakeVisible(tuningDrawerButton_);

    configureLabel(drawerTitleLabel_, 13.0f, juce::Font::bold, juce::Justification::centredLeft);
    drawerTitleLabel_.setText("TUNING CONFIGURATION", juce::dontSendNotification);
    drawerTitleLabel_.setColour(juce::Label::textColourId, accentColour);

    styleComboBox(tuningBox_);
    tuningBox_.onChange = [this] {
        processor_.setTuningIndex(tuningBox_.getSelectedId() - 1);
        refreshTuningList();
    };
    addAndMakeVisible(tuningBox_);

    configureLabel(a4Label_, 12.0f, juce::Font::bold, juce::Justification::centredLeft);
    a4Label_.setText("A4 CALIBRATION", juce::dontSendNotification);
    a4Label_.setColour(juce::Label::textColourId, secondaryTextColour);

    a4Slider_.setRange(432.0, 448.0, 0.1);
    a4Slider_.setValue(processor_.a4Hz(), juce::dontSendNotification);
    a4Slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 26);
    a4Slider_.setTextValueSuffix(" Hz");
    a4Slider_.setColour(juce::Slider::thumbColourId, accentColour);
    a4Slider_.setColour(juce::Slider::trackColourId, accentColour);
    a4Slider_.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff34383b));
    a4Slider_.setColour(juce::Slider::textBoxTextColourId, textColour);
    a4Slider_.setColour(juce::Slider::textBoxOutlineColourId, borderColour);
    a4Slider_.onValueChange = [this] {
        processor_.setA4Hz(a4Slider_.getValue());
    };
    addAndMakeVisible(a4Slider_);

    const auto noteChoices = editableNoteNames();
    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        stringLabels_[i].setText("STRING " + juce::String(static_cast<int>(6 - i)), juce::dontSendNotification);
        stringLabels_[i].setJustificationType(juce::Justification::centred);
        stringLabels_[i].setColour(juce::Label::textColourId, secondaryTextColour);
        stringLabels_[i].setFont(juce::FontOptions(11.0f, juce::Font::bold));
        addAndMakeVisible(stringLabels_[i]);

        styleComboBox(stringEditors_[i]);
        for (int choiceIndex = 0; choiceIndex < static_cast<int>(noteChoices.size()); ++choiceIndex) {
            stringEditors_[i].addItem(noteChoices[static_cast<std::size_t>(choiceIndex)], choiceIndex + 1);
        }
        addAndMakeVisible(stringEditors_[i]);
    }

    auto configureDrawerButton = [this](juce::TextButton& button) {
        button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button.setColour(juce::TextButton::buttonColourId, raisedPanelColour);
        button.setColour(juce::TextButton::buttonOnColourId, accentColour.withAlpha(0.3f));
        button.setColour(juce::TextButton::textColourOffId, textColour);
        button.setColour(juce::TextButton::textColourOnId, textColour);
        addAndMakeVisible(button);
    };

    configureDrawerButton(saveCustomButton_);
    saveCustomButton_.setButtonText("SAVE AS CUSTOM");
    saveCustomButton_.onClick = [this] {
        applyCustomTuningFromEditors();
        refreshTuningList();
    };

    configureDrawerButton(doneButton_);
    doneButton_.setButtonText("DONE");
    doneButton_.onClick = [this] { setDrawerOpen(false); };

    refreshTuningList();
    setDrawerOpen(false);
    currentSnapshot_ = processor_.snapshot();
    timerCallback();
    startTimerHz(30);
}

void GravePitchAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour);
    graphics.setColour(borderColour.withAlpha(0.7f));
    graphics.drawRect(getLocalBounds().reduced(3), 1);

    graphics.setColour(accentColour.withAlpha(0.78f));
    graphics.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    graphics.drawFittedText("GRAVEPITCH", {18, 14, 260, 38}, juce::Justification::centredLeft, 1);

    graphics.setColour(panelColour);
    graphics.fillRoundedRectangle(meterBounds_, 6.0f);
    graphics.setColour(borderColour.withAlpha(0.65f));
    graphics.drawRoundedRectangle(meterBounds_, 6.0f, 1.0f);

    const auto scaleBounds = meterBounds_.reduced(36.0f, 0.0f).withTrimmedTop(38.0f).withHeight(58.0f);
    const float scaleTop = scaleBounds.getY();
    const float scaleBottom = scaleBounds.getBottom();
    const float accurateLeft = centsToX(scaleBounds, -5.0);
    const float accurateRight = centsToX(scaleBounds, 5.0);

    graphics.setColour(inTuneColour.withAlpha(0.24f));
    graphics.fillRect(juce::Rectangle<float>(accurateLeft, scaleTop - 6.0f, accurateRight - accurateLeft, scaleBottom - scaleTop + 12.0f));
    graphics.setColour(inTuneColour.withAlpha(0.75f));
    graphics.drawVerticalLine(static_cast<int>(accurateLeft), scaleTop - 6.0f, scaleBottom + 6.0f);
    graphics.drawVerticalLine(static_cast<int>(accurateRight), scaleTop - 6.0f, scaleBottom + 6.0f);

    graphics.setColour(accentColour);
    graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    graphics.drawText("FLAT", meterBounds_.toNearestInt().reduced(18).removeFromTop(24), juce::Justification::centredLeft);
    graphics.drawText("SHARP", meterBounds_.toNearestInt().reduced(18).removeFromTop(24), juce::Justification::centredRight);

    for (int cents = -50; cents <= 50; cents += 5) {
        const float x = centsToX(scaleBounds, static_cast<double>(cents));
        const bool isLabelled = cents == -50 || cents == -25 || cents == -10 || cents == 0
            || cents == 10 || cents == 25 || cents == 50;
        const float tickHeight = isLabelled ? 22.0f : 11.0f;

        graphics.setColour(cents == 0 ? textColour : accentColour.withAlpha(isLabelled ? 0.9f : 0.72f));
        graphics.drawLine(x, scaleTop + 8.0f, x, scaleTop + 8.0f + tickHeight, cents == 0 ? 2.0f : 1.4f);

        if (isLabelled) {
            const auto label = cents > 0 ? "+" + juce::String(cents) : juce::String(cents);
            graphics.setFont(juce::FontOptions(11.0f, cents == 0 ? juce::Font::bold : juce::Font::plain));
            graphics.drawText(label, juce::Rectangle<float>(x - 22.0f, scaleBottom - 4.0f, 44.0f, 20.0f), juce::Justification::centred);
        }
    }

    const float centerX = centsToX(scaleBounds, 0.0);
    graphics.setColour(textColour);
    graphics.drawLine(centerX, scaleTop - 7.0f, centerX, scaleBottom - 5.0f, 2.0f);

    if (currentSnapshot_.hasPitch) {
        const float opacity = static_cast<float>(std::clamp(currentSnapshot_.displayOpacity, 0.0, 1.0));
        const float indicatorX = centsToX(scaleBounds, currentSnapshot_.cents);
        graphics.setColour(accentColour.withMultipliedAlpha(opacity));
        graphics.fillRect(juce::Rectangle<float>(indicatorX - 2.5f, scaleTop - 6.0f, 5.0f, scaleBottom - scaleTop + 1.0f));

        juce::Path marker;
        marker.addTriangle(indicatorX - 6.0f, scaleTop - 13.0f, indicatorX + 6.0f, scaleTop - 13.0f, indicatorX, scaleTop - 5.0f);
        graphics.fillPath(marker);

        if (currentSnapshot_.cents < -50.0 || currentSnapshot_.cents > 50.0) {
            const bool isLeft = currentSnapshot_.cents < 0.0;
            const float arrowX = isLeft ? meterBounds_.getX() + 20.0f : meterBounds_.getRight() - 20.0f;
            const float arrowY = meterBounds_.getCentreY() + 8.0f;
            juce::Path arrow;
            if (isLeft) {
                arrow.addTriangle(arrowX - 7.0f, arrowY, arrowX + 3.0f, arrowY - 7.0f, arrowX + 3.0f, arrowY + 7.0f);
            } else {
                arrow.addTriangle(arrowX + 7.0f, arrowY, arrowX - 3.0f, arrowY - 7.0f, arrowX - 3.0f, arrowY + 7.0f);
            }
            graphics.fillPath(arrow);
        }
    }

    if (drawerOpen_) {
        graphics.setColour(backgroundColour.withAlpha(0.72f));
        graphics.fillRect(drawerBounds_);
        graphics.setColour(raisedPanelColour);
        graphics.fillRoundedRectangle(drawerBounds_.toFloat(), 6.0f);
        graphics.setColour(borderColour);
        graphics.drawRoundedRectangle(drawerBounds_.toFloat(), 6.0f, 1.2f);

        auto drawerHeader = drawerBounds_;
        const auto topRight = drawerHeader.removeFromTop(30).removeFromRight(34).toFloat();
        juce::Path chevron;
        chevron.startNewSubPath(topRight.getX() + 7.0f, topRight.getCentreY() + 3.0f);
        chevron.lineTo(topRight.getCentreX(), topRight.getCentreY() - 3.0f);
        chevron.lineTo(topRight.getRight() - 7.0f, topRight.getCentreY() + 3.0f);
        graphics.setColour(accentColour);
        graphics.strokePath(chevron, juce::PathStrokeType(1.8f));
    }
}

void GravePitchAudioProcessorEditor::resized()
{
    levelLabel_.setBounds(302, 18, 150, 32);
    muteButton_.setBounds(480, 18, 128, 34);
    statusLabel_.setBounds(620, 18, 122, 32);

    noteLabel_.setBounds(248, 64, 264, 112);
    stringLabel_.setBounds(280, 168, 200, 28);
    frequencyLabel_.setBounds(36, 198, 220, 30);
    centsLabel_.setBounds(518, 198, 206, 30);

    meterBounds_ = juce::Rectangle<float>(18.0f, 230.0f, 724.0f, 126.0f);
    tuningDrawerButton_.setBounds(250, 390, 260, 42);

    drawerBounds_ = {18, 268, 724, 174};
    drawerTitleLabel_.setBounds(34, 273, 250, 23);
    tuningBox_.setBounds(34, 301, 236, 32);
    a4Label_.setBounds(292, 301, 112, 32);
    a4Slider_.setBounds(402, 301, 312, 32);

    constexpr int stringGap = 7;
    constexpr int stringWidth = 108;
    const int stringsX = 35;
    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        const int x = stringsX + static_cast<int>(i) * (stringWidth + stringGap);
        stringLabels_[i].setBounds(x, 338, stringWidth, 18);
        stringEditors_[i].setBounds(x, 357, stringWidth, 30);
    }

    saveCustomButton_.setBounds(218, 400, 166, 31);
    doneButton_.setBounds(396, 400, 146, 31);
}

void GravePitchAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (drawerOpen_ && !drawerBounds_.contains(event.getPosition())) {
        setDrawerOpen(false);
    }
}

void GravePitchAudioProcessorEditor::timerCallback()
{
    currentSnapshot_ = processor_.snapshot();
    const float pitchOpacity = currentSnapshot_.hasPitch
        ? static_cast<float>(std::clamp(currentSnapshot_.displayOpacity, 0.0, 1.0))
        : 1.0f;

    for (auto* label : {&noteLabel_, &stringLabel_, &frequencyLabel_, &centsLabel_}) {
        label->setAlpha(pitchOpacity);
    }

    noteLabel_.setText(currentSnapshot_.hasPitch ? currentSnapshot_.noteName : "--", juce::dontSendNotification);
    stringLabel_.setText(
        currentSnapshot_.stringNumber > 0 ? "STRING " + juce::String(currentSnapshot_.stringNumber) : "STRING --",
        juce::dontSendNotification);
    frequencyLabel_.setText(formatFrequency(currentSnapshot_.frequencyHz), juce::dontSendNotification);
    centsLabel_.setText(currentSnapshot_.hasPitch ? formatCents(currentSnapshot_.cents) : "-- cents", juce::dontSendNotification);
    levelLabel_.setText(
        "INPUT  " + juce::String(juce::Decibels::gainToDecibels(currentSnapshot_.rms, -90.0), 1) + " dBFS",
        juce::dontSendNotification);

    const auto state = statusText(currentSnapshot_);
    statusLabel_.setText(state, juce::dontSendNotification);
    statusLabel_.setColour(
        juce::Label::textColourId,
        state == "IN TUNE" ? inTuneColour : (state == "NO SIGNAL" ? secondaryTextColour : accentColour));

    muteButton_.setToggleState(processor_.outputMuted(), juce::dontSendNotification);
    if (drawerOpen_ && std::abs(a4Slider_.getValue() - processor_.a4Hz()) >= 0.05) {
        a4Slider_.setValue(processor_.a4Hz(), juce::dontSendNotification);
    }

    repaint();
}

void GravePitchAudioProcessorEditor::refreshTuningList()
{
    const auto allTunings = processor_.tunings();
    const int selectedIndex = processor_.tuningIndex();

    tuningBox_.clear(juce::dontSendNotification);
    for (int i = 0; i < static_cast<int>(allTunings.size()); ++i) {
        tuningBox_.addItem(allTunings[static_cast<std::size_t>(i)].name, i + 1);
    }
    tuningBox_.setSelectedId(selectedIndex + 1, juce::dontSendNotification);

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(allTunings.size())) {
        const auto& notes = allTunings[static_cast<std::size_t>(selectedIndex)].midiNotesLowToHigh;
        for (std::size_t i = 0; i < stringEditors_.size() && i < notes.size(); ++i) {
            stringEditors_[i].setText(gravepitch::noteNameForMidi(notes[i]), juce::dontSendNotification);
        }
    }

    updateTuningButtonText();
}

void GravePitchAudioProcessorEditor::applyCustomTuningFromEditors()
{
    std::vector<juce::String> notes;
    notes.reserve(stringEditors_.size());

    for (const auto& editor : stringEditors_) {
        notes.push_back(editor.getText());
    }

    processor_.setCustomTuning(notes);
}

void GravePitchAudioProcessorEditor::setDrawerOpen(bool shouldOpen)
{
    drawerOpen_ = shouldOpen;
    tuningDrawerButton_.setVisible(!drawerOpen_);

    const std::array<juce::Component*, 6> drawerComponents {
        &drawerTitleLabel_, &tuningBox_, &a4Label_, &a4Slider_, &saveCustomButton_, &doneButton_
    };
    for (auto* component : drawerComponents) {
        component->setVisible(drawerOpen_);
    }
    for (auto& label : stringLabels_) {
        label.setVisible(drawerOpen_);
    }
    for (auto& editor : stringEditors_) {
        editor.setVisible(drawerOpen_);
    }

    if (drawerOpen_) {
        refreshTuningList();
        a4Slider_.setValue(processor_.a4Hz(), juce::dontSendNotification);
    }

    repaint();
}

void GravePitchAudioProcessorEditor::updateTuningButtonText()
{
    const auto allTunings = processor_.tunings();
    const int selectedIndex = processor_.tuningIndex();
    const auto name = selectedIndex >= 0 && selectedIndex < static_cast<int>(allTunings.size())
        ? juce::String(allTunings[static_cast<std::size_t>(selectedIndex)].name).toUpperCase()
        : juce::String("TUNING");
    tuningDrawerButton_.setButtonText(name + "  ·  6 STRING    v");
}

void GravePitchAudioProcessorEditor::styleComboBox(juce::ComboBox& comboBox)
{
    comboBox.setColour(juce::ComboBox::backgroundColourId, panelColour);
    comboBox.setColour(juce::ComboBox::textColourId, textColour);
    comboBox.setColour(juce::ComboBox::outlineColourId, borderColour);
    comboBox.setColour(juce::ComboBox::arrowColourId, accentColour);
    comboBox.setColour(juce::ComboBox::focusedOutlineColourId, accentColour);
}
