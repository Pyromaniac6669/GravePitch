#include "PluginEditor.h"

#include "BinaryData.h"
#include "gravepitch/core/Note.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

const auto accentColour = juce::Colour(0xffc99655);
const auto secondaryTextColour = juce::Colour(0xff8e8b88);
constexpr int editorWidth = 920;
constexpr int editorHeight = 520;
constexpr float scaleLeft = 58.0f;
constexpr float scaleRight = 862.0f;

juce::Image imageFromMemory(const void* data, int size)
{
    return juce::ImageCache::getFromMemory(data, size);
}

std::vector<juce::String> editableNoteNames()
{
    constexpr std::array<const char*, 12> sharpNames {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    std::vector<juce::String> names;

    for (int octave = 1; octave <= 5; ++octave) {
        for (std::size_t pitchClass = 0; pitchClass < sharpNames.size(); ++pitchClass) {
            if (octave == 5 && pitchClass > 4) {
                break;
            }

            names.emplace_back(juce::String(sharpNames[pitchClass]) + juce::String(octave));
        }
    }

    return names;
}

float centsToX(const juce::Rectangle<float>& scaleBounds, double cents)
{
    return juce::jmap(
        GravePitchAudioProcessorEditor::scalePositionForCents(cents),
        scaleBounds.getX(),
        scaleBounds.getRight());
}

} // namespace

MuteToggleButton::MuteToggleButton()
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MuteToggleButton::paintButton(juce::Graphics& graphics, bool isMouseOver, bool isButtonDown)
{
    const auto image = getToggleState()
        ? imageFromMemory(BinaryData::mute_on_2x_png, BinaryData::mute_on_2x_pngSize)
        : imageFromMemory(BinaryData::mute_off_2x_png, BinaryData::mute_off_2x_pngSize);
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImage(image, getLocalBounds().toFloat());

    if (isMouseOver || isButtonDown) {
        graphics.setColour(accentColour.withAlpha(isButtonDown ? 0.22f : 0.10f));
        graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 16.0f, 1.0f);
    }
}

InTuneIndicator::InTuneIndicator()
{
    setComponentID("inTuneIndicator");
    setInterceptsMouseClicks(false, false);
}

void InTuneIndicator::paint(juce::Graphics& graphics)
{
    const auto image = active_
        ? imageFromMemory(BinaryData::in_tune_on_2x_png, BinaryData::in_tune_on_2x_pngSize)
        : imageFromMemory(BinaryData::in_tune_off_2x_png, BinaryData::in_tune_off_2x_pngSize);
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImage(image, getLocalBounds().toFloat());
}

void InTuneIndicator::setActive(bool shouldBeActive)
{
    if (active_ == shouldBeActive) {
        return;
    }

    active_ = shouldBeActive;
    repaint();
}

bool InTuneIndicator::isActive() const noexcept
{
    return active_;
}

bool InTuneIndicator::shouldBeActiveFor(const GravePitchSnapshot& snapshot) noexcept
{
    return snapshot.hasPitch && snapshot.stable && std::abs(snapshot.cents) <= 5.0;
}

GravePitchAudioProcessorEditor::GravePitchAudioProcessorEditor(GravePitchAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor)
    , processor_(audioProcessor)
{
    setOpaque(true);
    setResizeLimits(editorWidth, editorHeight, editorWidth, editorHeight);
    setSize(editorWidth, editorHeight);

    mainPlate_ = imageFromMemory(BinaryData::main_plate_clean_wide_2x_png, BinaryData::main_plate_clean_wide_2x_pngSize);
    brandLogo_ = imageFromMemory(BinaryData::gravepitch_logo_2x_png, BinaryData::gravepitch_logo_2x_pngSize);
    uiTypeface_ = juce::Typeface::createSystemTypefaceFor(
        BinaryData::OswaldVariable_ttf, static_cast<std::size_t>(BinaryData::OswaldVariable_ttfSize));
    drawerLookAndFeel_.setDefaultSansSerifTypeface(uiTypeface_);
    drawerLookAndFeel_.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff151819));
    drawerLookAndFeel_.setColour(juce::PopupMenu::textColourId, juce::Colour(0xffd4d0c9));
    drawerLookAndFeel_.setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff352a1e));
    drawerLookAndFeel_.setColour(juce::PopupMenu::highlightedTextColourId, accentColour);

    muteButton_.setToggleState(processor_.outputMuted(), juce::dontSendNotification);
    muteButton_.onClick = [this] {
        processor_.setOutputMuted(muteButton_.getToggleState());
    };
    addAndMakeVisible(muteButton_);
    addAndMakeVisible(inTuneIndicator_);

    tuningDrawerButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    tuningDrawerButton_.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    tuningDrawerButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    tuningDrawerButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
    tuningDrawerButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
    tuningDrawerButton_.onClick = [this] { setDrawerOpen(true); };
    addAndMakeVisible(tuningDrawerButton_);

    styleComboBox(tuningBox_);
    tuningBox_.setLookAndFeel(&drawerLookAndFeel_);
    tuningBox_.onChange = [this] {
        processor_.setTuningIndex(tuningBox_.getSelectedId() - 1);
        refreshTuningList();
    };
    addAndMakeVisible(tuningBox_);

    a4Slider_.setRange(432.0, 448.0, 1.0);
    a4Slider_.setValue(std::round(processor_.a4Hz()), juce::dontSendNotification);
    a4Slider_.setDoubleClickReturnValue(true, 440.0);
    a4Slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    a4Slider_.setSliderSnapsToMousePosition(true);
    a4Slider_.setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    a4Slider_.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    a4Slider_.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    a4Slider_.setColour(juce::Slider::textBoxTextColourId, juce::Colours::transparentBlack);
    a4Slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    a4Slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    a4Slider_.onValueChange = [this] {
        processor_.setA4Hz(a4Slider_.getValue());
        repaint();
    };
    addAndMakeVisible(a4Slider_);

    const auto noteChoices = editableNoteNames();
    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        styleComboBox(stringEditors_[i]);
        stringEditors_[i].setLookAndFeel(&drawerLookAndFeel_);
        for (int choiceIndex = 0; choiceIndex < static_cast<int>(noteChoices.size()); ++choiceIndex) {
            stringEditors_[i].addItem(noteChoices[static_cast<std::size_t>(choiceIndex)], choiceIndex + 1);
        }
        stringEditors_[i].onChange = [this] { repaint(); };
        addAndMakeVisible(stringEditors_[i]);
    }

    auto configureDrawerButton = [this](juce::TextButton& button) {
        button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::transparentBlack);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::transparentBlack);
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

juce::Font GravePitchAudioProcessorEditor::uiFont(float height) const
{
    return juce::Font(juce::FontOptions(uiTypeface_).withHeight(height));
}

float GravePitchAudioProcessorEditor::scalePositionForCents(double cents) noexcept
{
    return static_cast<float>((std::clamp(cents, -50.0, 50.0) + 50.0) / 100.0);
}

juce::String GravePitchAudioProcessorEditor::noteNameWithoutOctave(juce::String noteName)
{
    while (noteName.isNotEmpty()) {
        const auto lastCharacter = noteName.getLastCharacter();
        if (!juce::CharacterFunctions::isDigit(lastCharacter) && lastCharacter != '-') {
            break;
        }

        noteName = noteName.dropLastCharacters(1);
    }

    return noteName;
}

void GravePitchAudioProcessorEditor::drawPitchReadout(juce::Graphics& graphics) const
{
    const auto opacity = currentSnapshot_.hasPitch
        ? static_cast<float>(std::clamp(currentSnapshot_.displayOpacity, 0.0, 1.0))
        : 1.0f;
    const auto noteText = currentSnapshot_.hasPitch
        ? noteNameWithoutOctave(currentSnapshot_.noteName)
        : juce::String("--");
    const auto stringText = currentSnapshot_.stringNumber > 0
        ? "STRING " + juce::String(currentSnapshot_.stringNumber)
        : juce::String("STRING --");
    const juce::Rectangle<float> noteBounds(325.0f, 82.0f, 270.0f, 146.0f);
    const juce::Rectangle<int> stringBounds(340, 219, 240, 31);

    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(uiFont(150.0f), noteText,
        noteBounds.getX(), noteBounds.getY(), noteBounds.getWidth(), noteBounds.getHeight(),
        juce::Justification::centred, 1, 0.72f);
    juce::Path notePath;
    glyphs.createPath(notePath);
    const auto rawPathBounds = notePath.getBounds();
    notePath.applyTransform(juce::AffineTransform::scale(
        1.58f, 1.28f, rawPathBounds.getCentreX(), rawPathBounds.getCentreY()));
    notePath.applyTransform(juce::AffineTransform::translation(0.0f, -8.0f));

    juce::Graphics::ScopedSaveState noteState(graphics);
    graphics.setOpacity(opacity);
    graphics.setColour(juce::Colours::black.withAlpha(0.72f));
    graphics.fillPath(notePath, juce::AffineTransform::translation(1.2f, 2.0f));
    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xfff0efec), noteBounds.getCentreX(), noteBounds.getY(),
        juce::Colour(0xffaaa7a3), noteBounds.getCentreX(), noteBounds.getBottom(), false));
    graphics.fillPath(notePath);

    {
        juce::Graphics::ScopedSaveState textureState(graphics);
        graphics.reduceClipRegion(notePath);
        graphics.setColour(juce::Colour(0xff4f4b47).withAlpha(0.13f));
        for (int index = 0; index < 52; ++index) {
            const auto x = noteBounds.getX() + std::fmod(static_cast<float>(index * 47), noteBounds.getWidth() - 18.0f) + 8.0f;
            const auto y = noteBounds.getY() + std::fmod(static_cast<float>(index * 31), noteBounds.getHeight() - 10.0f) + 4.0f;
            const auto length = 2.0f + static_cast<float>(index % 8);
            const auto rise = static_cast<float>((index % 3) - 1) * 0.7f;
            graphics.drawLine(x, y, x + length, y + rise, 0.45f);
        }
    }

    auto stringFont = uiFont(28.0f).withExtraKerningFactor(0.08f);
    graphics.setFont(stringFont);
    graphics.setColour(juce::Colours::black.withAlpha(0.75f));
    graphics.drawFittedText(stringText, stringBounds.translated(1, 2), juce::Justification::centred, 1);
    graphics.setColour(secondaryTextColour);
    graphics.drawFittedText(stringText, stringBounds, juce::Justification::centred, 1);
}

void GravePitchAudioProcessorEditor::drawTuningScale(juce::Graphics& graphics) const
{
    const juce::Rectangle<float> scaleBounds(scaleLeft, 0.0f, scaleRight - scaleLeft, 1.0f);
    const auto inTuneLeft = centsToX(scaleBounds, -5.0);
    const auto inTuneRight = centsToX(scaleBounds, 5.0);
    const auto centreX = centsToX(scaleBounds, 0.0);
    constexpr float bandTop = 317.0f;
    constexpr float bandBottom = 401.0f;
    constexpr float majorTop = 340.0f;
    constexpr float majorBottom = 372.0f;
    constexpr float minorTop = 348.0f;
    constexpr float minorBottom = 367.0f;
    constexpr int labelY = 375;
    constexpr int labelHeight = 28;
    constexpr float centreTop = 326.0f;
    constexpr float centreBottom = 369.0f;

    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff384035).withAlpha(0.42f), inTuneLeft, bandTop,
        juce::Colour(0xff20251f).withAlpha(0.26f), inTuneRight, bandBottom, false));
    graphics.fillRect(juce::Rectangle<float>(inTuneLeft, bandTop, inTuneRight - inTuneLeft, bandBottom - bandTop));

    graphics.setColour(juce::Colour(0xffd0b286).withAlpha(0.88f));
    for (const auto boundaryX : { inTuneLeft, inTuneRight }) {
        for (float y = bandTop; y < bandBottom; y += 7.0f) {
            graphics.drawLine(boundaryX, y, boundaryX, std::min(y + 3.0f, bandBottom), 1.0f);
        }
    }

    constexpr std::array<int, 7> labelledCents { -50, -25, -10, 0, 10, 25, 50 };
    auto isLabelled = [](int cents) {
        return cents == -50 || cents == -25 || cents == -10 || cents == 0
            || cents == 10 || cents == 25 || cents == 50;
    };

    for (int cents = -50; cents <= 50; cents += 5) {
        if (cents == 0) {
            continue;
        }

        const auto x = centsToX(scaleBounds, static_cast<double>(cents));
        const auto major = isLabelled(cents);
        graphics.setColour(juce::Colour(0xffc3a180).withAlpha(major ? 0.96f : 0.84f));
        graphics.drawLine(x, major ? majorTop : minorTop, x, major ? majorBottom : minorBottom, major ? 1.7f : 1.2f);
    }

    graphics.setColour(juce::Colour(0xffe7e5df).withAlpha(0.95f));
    graphics.drawLine(centreX, centreTop, centreX, centreBottom, 1.8f);

    graphics.setFont(uiFont(19.0f).withExtraKerningFactor(0.04f));
    graphics.setColour(accentColour);
    for (const auto cents : labelledCents) {
        const auto x = centsToX(scaleBounds, static_cast<double>(cents));
        const auto text = cents > 0 ? "+" + juce::String(cents) : juce::String(cents);
        graphics.drawFittedText(text, { juce::roundToInt(x) - 28, labelY, 56, labelHeight }, juce::Justification::centred, 1);
    }

    constexpr float arrowY = 358.0f;
    juce::Path leftArrow;
    leftArrow.addTriangle(scaleLeft - 22.0f, arrowY, scaleLeft - 12.0f, arrowY - 7.0f, scaleLeft - 12.0f, arrowY + 7.0f);
    juce::Path rightArrow;
    rightArrow.addTriangle(scaleRight + 22.0f, arrowY, scaleRight + 12.0f, arrowY - 7.0f, scaleRight + 12.0f, arrowY + 7.0f);
    graphics.fillPath(leftArrow);
    graphics.fillPath(rightArrow);
}

void GravePitchAudioProcessorEditor::drawMovingIndicator(juce::Graphics& graphics) const
{
    if (!currentSnapshot_.hasPitch) {
        return;
    }

    const auto opacity = static_cast<float>(std::clamp(currentSnapshot_.displayOpacity, 0.0, 1.0));
    const juce::Rectangle<float> scaleBounds(scaleLeft, 0.0f, scaleRight - scaleLeft, 1.0f);
    const auto indicatorX = centsToX(scaleBounds, currentSnapshot_.cents);
    constexpr float bladeTop = 323.0f;
    constexpr float bladeBottom = 369.0f;

    juce::Graphics::ScopedSaveState state(graphics);
    graphics.setOpacity(opacity);
    graphics.setColour(juce::Colour(0xffe1a54c).withAlpha(0.13f));
    graphics.fillRoundedRectangle(indicatorX - 4.5f, bladeTop - 1.0f, 9.0f, bladeBottom - bladeTop + 2.0f, 4.0f);

    juce::Path blade;
    blade.startNewSubPath(indicatorX - 2.8f, bladeTop);
    blade.lineTo(indicatorX + 2.8f, bladeTop);
    blade.lineTo(indicatorX + 1.7f, bladeBottom - 5.0f);
    blade.lineTo(indicatorX, bladeBottom);
    blade.lineTo(indicatorX - 1.7f, bladeBottom - 5.0f);
    blade.closeSubPath();
    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xffffdfa0), indicatorX - 3.0f, bladeTop,
        juce::Colour(0xffa65f18), indicatorX + 3.0f, bladeTop, false));
    graphics.fillPath(blade);
    graphics.setColour(juce::Colour(0xff44240f).withAlpha(0.82f));
    graphics.strokePath(blade, juce::PathStrokeType(0.7f));

    juce::Path marker;
    marker.addTriangle(indicatorX - 6.0f, bladeTop - 12.0f, indicatorX + 6.0f, bladeTop - 12.0f, indicatorX, bladeTop - 3.0f);
    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xffffd178), indicatorX, bladeTop - 12.0f,
        juce::Colour(0xff9c5618), indicatorX, bladeTop - 3.0f, false));
    graphics.fillPath(marker);
}

juce::String GravePitchAudioProcessorEditor::currentTuningName() const
{
    const auto allTunings = processor_.tunings();
    const auto selectedIndex = processor_.tuningIndex();
    return selectedIndex >= 0 && selectedIndex < static_cast<int>(allTunings.size())
        ? juce::String(allTunings[static_cast<std::size_t>(selectedIndex)].name).toUpperCase()
        : juce::String("TUNING");
}

void GravePitchAudioProcessorEditor::drawDrawerValues(juce::Graphics& graphics) const
{
    graphics.setFont(uiFont(17.0f).withExtraKerningFactor(0.08f));
    graphics.setColour(accentColour);
    graphics.drawFittedText(currentTuningName(), {142, 334, 245, 32}, juce::Justification::centredLeft, 1);
    graphics.drawFittedText(juce::String(juce::roundToInt(a4Slider_.getValue())), {764, 334, 78, 32}, juce::Justification::centred, 1);
    graphics.drawFittedText("Hz", {840, 334, 30, 32}, juce::Justification::centredLeft, 1);

    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        const auto x = 49 + static_cast<int>(i) * 138;
        graphics.setFont(uiFont(18.0f).withExtraKerningFactor(0.06f));
        graphics.drawFittedText(stringEditors_[i].getText(), {x, 412, 118, 31}, juce::Justification::centred, 1);
    }

    constexpr float trackLeft = 540.0f;
    constexpr float trackRight = 747.0f;
    constexpr float trackY = 349.0f;
    const auto thumbX = juce::jmap(static_cast<float>(a4Slider_.getValue()), 432.0f, 448.0f, trackLeft, trackRight);
    graphics.setColour(juce::Colour(0xff050708).withAlpha(0.92f));
    graphics.drawLine(trackLeft, trackY, trackRight, trackY, 3.4f);
    graphics.setColour(juce::Colour(0xff6f6962).withAlpha(0.48f));
    graphics.drawLine(trackLeft, trackY - 0.5f, trackRight, trackY - 0.5f, 0.8f);
    graphics.setColour(accentColour.withAlpha(0.92f));
    graphics.drawLine(trackLeft, trackY, thumbX, trackY, 1.6f);
    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xffffd58b), thumbX - 3.0f, trackY - 4.0f,
        juce::Colour(0xff8a5928), thumbX + 5.0f, trackY + 5.0f, true));
    graphics.fillEllipse(thumbX - 6.5f, trackY - 6.5f, 13.0f, 13.0f);
    graphics.setColour(juce::Colour(0xff2a2118));
    graphics.drawEllipse(thumbX - 7.0f, trackY - 7.0f, 14.0f, 14.0f, 1.0f);
}

void GravePitchAudioProcessorEditor::drawDrawerOverlay(juce::Graphics& graphics) const
{
    graphics.setColour(juce::Colours::black.withAlpha(0.42f));
    graphics.fillRoundedRectangle(drawerBounds_.toFloat().expanded(5.0f).translated(0.0f, 3.0f), 4.0f);

    const auto panel = drawerBounds_.toFloat();
    graphics.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff1a1c1d).withAlpha(0.98f), panel.getCentreX(), panel.getY(),
        juce::Colour(0xff0e1011).withAlpha(0.99f), panel.getCentreX(), panel.getBottom(), false));
    graphics.fillRoundedRectangle(panel, 4.0f);
    graphics.setColour(juce::Colour(0xff77716c).withAlpha(0.66f));
    graphics.drawRoundedRectangle(panel.reduced(0.5f), 4.0f, 1.0f);
    graphics.setColour(juce::Colour(0xffb0aaa2).withAlpha(0.09f));
    graphics.drawLine(panel.getX() + 5.0f, panel.getY() + 2.0f, panel.getRight() - 5.0f, panel.getY() + 2.0f, 0.8f);

    auto drawField = [&graphics](juce::Rectangle<float> bounds, bool accented = false) {
        graphics.setColour(juce::Colour(0xff080a0b).withAlpha(0.58f));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour((accented ? accentColour : juce::Colour(0xff77716c)).withAlpha(accented ? 0.92f : 0.68f));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.045f));
        graphics.drawLine(bounds.getX() + 3.0f, bounds.getY() + 1.5f, bounds.getRight() - 3.0f, bounds.getY() + 1.5f, 0.7f);
    };
    auto drawChevron = [&graphics](float centreX, float centreY, bool pointsUp = false) {
        juce::Path chevron;
        const auto direction = pointsUp ? -1.0f : 1.0f;
        chevron.startNewSubPath(centreX - 5.0f, centreY - 2.5f * direction);
        chevron.lineTo(centreX, centreY + 2.5f * direction);
        chevron.lineTo(centreX + 5.0f, centreY - 2.5f * direction);
        graphics.setColour(accentColour.withAlpha(0.92f));
        graphics.strokePath(chevron, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawField({129.0f, 334.0f, 284.0f, 32.0f});
    drawField({764.0f, 334.0f, 108.0f, 32.0f});
    drawChevron(400.0f, 350.0f);

    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        const auto x = 49.0f + static_cast<float>(i) * 138.0f;
        drawField({x, 412.0f, 118.0f, 31.0f});
        drawChevron(x + 106.0f, 427.5f);
    }

    drawField({294.0f, 463.0f, 140.0f, 31.0f});
    drawField({459.0f, 463.0f, 130.0f, 31.0f}, true);
    drawChevron(875.0f, 311.0f, true);

    graphics.setColour(juce::Colour(0xffd0cbc3));
    graphics.setFont(uiFont(19.0f).withExtraKerningFactor(0.08f));
    graphics.drawFittedText("TUNING CONFIGURATION", {36, 298, 260, 28}, juce::Justification::centredLeft, 1);

    graphics.setFont(uiFont(17.0f).withExtraKerningFactor(0.06f));
    graphics.drawFittedText("TUNING PRESET", {40, 332, 84, 32}, juce::Justification::centredLeft, 1);
    graphics.drawFittedText("A4 CALIBRATION", {442, 332, 92, 32}, juce::Justification::centredLeft, 1);

    graphics.setFont(uiFont(18.0f).withExtraKerningFactor(0.07f));
    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        const auto x = 49 + static_cast<int>(i) * 138;
        graphics.drawFittedText("STRING " + juce::String(6 - static_cast<int>(i)), {x, 382, 118, 28}, juce::Justification::centred, 1);
    }

    graphics.setFont(uiFont(17.0f).withExtraKerningFactor(0.10f));
    graphics.drawFittedText("SAVE AS CUSTOM", {294, 463, 140, 31}, juce::Justification::centred, 1);
    graphics.setColour(accentColour);
    graphics.drawFittedText("DONE", {459, 463, 130, 31}, juce::Justification::centred, 1);
}

void GravePitchAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImage(mainPlate_, getLocalBounds().toFloat());
    graphics.drawImage(brandLogo_, juce::Rectangle<float>(24.0f, 12.0f, 300.0f, 90.0f));
    drawPitchReadout(graphics);
    drawTuningScale(graphics);
    drawMovingIndicator(graphics);

    if (drawerOpen_) {
        drawDrawerOverlay(graphics);
        drawDrawerValues(graphics);
    } else {
        graphics.setFont(uiFont(19.0f).withExtraKerningFactor(0.14f));
        graphics.setColour(accentColour);
        graphics.drawFittedText(currentTuningName() + "  ·  6 STRING", {310, 437, 300, 39}, juce::Justification::centred, 1);
    }
}

void GravePitchAudioProcessorEditor::resized()
{
    muteButton_.setBounds(688, 13, 125, 42);
    inTuneIndicator_.setBounds(822, 7, 64, 62);

    tuningDrawerButton_.setBounds(310, 437, 300, 39);

    drawerBounds_ = {19, 292, 882, 215};
    tuningBox_.setBounds(129, 334, 284, 32);
    a4Slider_.setBounds(528, 334, 231, 30);

    constexpr int stringGap = 20;
    constexpr int stringWidth = 118;
    constexpr int stringsX = 49;
    for (std::size_t i = 0; i < stringEditors_.size(); ++i) {
        const int x = stringsX + static_cast<int>(i) * (stringWidth + stringGap);
        stringEditors_[i].setBounds(x, 412, stringWidth, 31);
    }

    saveCustomButton_.setBounds(294, 463, 140, 31);
    doneButton_.setBounds(459, 463, 130, 31);
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
    inTuneIndicator_.setActive(InTuneIndicator::shouldBeActiveFor(currentSnapshot_));

    muteButton_.setToggleState(processor_.outputMuted(), juce::dontSendNotification);
    const auto integerA4Hz = std::round(processor_.a4Hz());
    if (drawerOpen_ && std::abs(a4Slider_.getValue() - integerA4Hz) >= 0.5) {
        a4Slider_.setValue(integerA4Hz, juce::dontSendNotification);
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

    const std::array<juce::Component*, 4> drawerComponents {
        &tuningBox_, &a4Slider_, &saveCustomButton_, &doneButton_
    };
    for (auto* component : drawerComponents) {
        component->setVisible(drawerOpen_);
    }
    for (auto& editor : stringEditors_) {
        editor.setVisible(drawerOpen_);
    }

    if (drawerOpen_) {
        refreshTuningList();
        a4Slider_.setValue(std::round(processor_.a4Hz()), juce::dontSendNotification);
    }

    resized();
    repaint();
}

void GravePitchAudioProcessorEditor::updateTuningButtonText()
{
    tuningDrawerButton_.setButtonText(currentTuningName() + "  ·  6 STRING");
    repaint();
}

void GravePitchAudioProcessorEditor::styleComboBox(juce::ComboBox& comboBox)
{
    comboBox.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    comboBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    comboBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    comboBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    comboBox.setColour(juce::ComboBox::focusedOutlineColourId, juce::Colours::transparentBlack);
}
