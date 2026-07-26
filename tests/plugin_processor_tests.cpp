#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "BinaryData.h"

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

float colourLuminance(juce::Colour colour)
{
    return 0.2126f * colour.getRed() + 0.7152f * colour.getGreen() + 0.0722f * colour.getBlue();
}

float averageLuminance(const juce::Image& image, juce::Rectangle<int> area)
{
    double luminance = 0.0;
    int pixelCount = 0;

    for (int y = area.getY(); y < area.getBottom(); ++y) {
        for (int x = area.getX(); x < area.getRight(); ++x) {
            luminance += colourLuminance(image.getPixelAt(x, y));
            ++pixelCount;
        }
    }

    return pixelCount > 0 ? static_cast<float>(luminance / pixelCount) : 0.0f;
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

bool testUiLanguageStateRoundTripsAndDefaultsSafely()
{
    GravePitchAudioProcessor source;
    bool ok = expectTrue(source.uiLanguage() == GravePitchUiLanguage::english,
        "new instances default to English");
    source.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);

    juce::MemoryBlock state;
    source.getStateInformation(state);

    GravePitchAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    ok &= expectTrue(restored.uiLanguage() == GravePitchUiLanguage::simplifiedChinese,
        "simplified Chinese language state round-trips");

    juce::ValueTree invalidState("GravePitchState");
    invalidState.setProperty("a4Hz", 440.0, nullptr);
    invalidState.setProperty("selectedTuningIndex", 0, nullptr);
    invalidState.setProperty("customTuning", "", nullptr);
    invalidState.setProperty("uiLanguage", "unsupported", nullptr);
    juce::MemoryBlock invalidData;
    if (auto xml = invalidState.createXml()) {
        juce::AudioProcessor::copyXmlToBinary(*xml, invalidData);
    }

    restored.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);
    restored.setStateInformation(invalidData.getData(), static_cast<int>(invalidData.getSize()));
    ok &= expectTrue(restored.uiLanguage() == GravePitchUiLanguage::english,
        "unknown language state falls back to English");

    juce::ValueTree legacyState("GravePitchState");
    legacyState.setProperty("a4Hz", 440.0, nullptr);
    legacyState.setProperty("selectedTuningIndex", 0, nullptr);
    legacyState.setProperty("customTuning", "", nullptr);
    juce::MemoryBlock legacyData;
    if (auto xml = legacyState.createXml()) {
        juce::AudioProcessor::copyXmlToBinary(*xml, legacyData);
    }

    restored.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);
    restored.setStateInformation(legacyData.getData(), static_cast<int>(legacyData.getSize()));
    ok &= expectTrue(restored.uiLanguage() == GravePitchUiLanguage::english,
        "legacy state without a language defaults to English");
    return ok;
}

bool testSimplifiedChineseEditorTextAndInstanceIsolation()
{
    GravePitchAudioProcessor chineseProcessor;
    chineseProcessor.setCustomTuning({"D2", "G2", "C3", "F3", "A3", "D4"});
    chineseProcessor.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);
    GravePitchAudioProcessor independentProcessor;
    independentProcessor.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);

    std::unique_ptr<juce::AudioProcessorEditor> editor(chineseProcessor.createEditor());
    bool ok = expectTrue(editor != nullptr, "simplified Chinese editor is created");
    if (editor == nullptr) {
        return false;
    }

    juce::TextButton* moreMenuButton = nullptr;
    juce::TextButton* tuningDrawerButton = nullptr;
    juce::TextButton* saveButton = nullptr;
    juce::ComboBox* draftStringEditor = nullptr;
    bool foundLocalizedDrawerButton = false;
    bool foundLocalizedSaveButton = false;
    bool foundLocalizedDoneButton = false;
    bool foundLocalizedCustomTuning = false;

    for (int index = 0; index < editor->getNumChildComponents(); ++index) {
        auto* component = editor->getChildComponent(index);
        if (auto* button = dynamic_cast<juce::TextButton*>(component)) {
            if (button->getComponentID() == "moreMenuButton") {
                moreMenuButton = button;
            } else if (button->getComponentID() == "tuningDrawerButton") {
                tuningDrawerButton = button;
                foundLocalizedDrawerButton = button->getButtonText().contains(juce::String::fromUTF8("自定义"))
                    && button->getButtonText().contains(juce::String::fromUTF8("6 弦"));
            } else if (button->getComponentID() == "saveCustomButton") {
                saveButton = button;
                foundLocalizedSaveButton = button->getButtonText() == juce::String::fromUTF8("保存为自定义");
            } else if (button->getComponentID() == "doneButton") {
                foundLocalizedDoneButton = button->getButtonText() == juce::String::fromUTF8("完成");
            }
        }

        if (auto* comboBox = dynamic_cast<juce::ComboBox*>(component)) {
            if (comboBox->getNumItems() == 7) {
                foundLocalizedCustomTuning = comboBox->getItemText(0) == "Standard"
                    && comboBox->getItemText(2) == "D Standard"
                    && comboBox->getItemText(6) == juce::String::fromUTF8("自定义");
            } else if (comboBox->getNumItems() == 53 && draftStringEditor == nullptr) {
                draftStringEditor = comboBox;
            }
        }
    }

    ok &= expectTrue(moreMenuButton != nullptr,
        "editor exposes the general overflow menu");
    ok &= expectTrue(moreMenuButton != nullptr
            && moreMenuButton->getBounds() == juce::Rectangle<int>(887, 17, 25, 32),
        "more menu keeps the unobtrusive top-right bounds");
    ok &= expectTrue(moreMenuButton != nullptr
            && moreMenuButton->getTitle() == juce::String::fromUTF8("More / 更多"),
        "more menu uses a bilingual accessible title");
    ok &= expectTrue(foundLocalizedDrawerButton, "collapsed tuning control localizes only its generic suffix");
    ok &= expectTrue(foundLocalizedSaveButton, "save custom action is localized");
    ok &= expectTrue(foundLocalizedDoneButton, "done action is localized");
    ok &= expectTrue(foundLocalizedCustomTuning,
        "custom tuning is localized while built-in tuning names stay English");

    if (tuningDrawerButton != nullptr && tuningDrawerButton->onClick != nullptr) {
        tuningDrawerButton->onClick();
    }
    if (draftStringEditor != nullptr) {
        draftStringEditor->setSelectedId(1, juce::sendNotificationSync);
    }
    const auto draftBeforeLanguageSwitch = draftStringEditor != nullptr
        ? draftStringEditor->getText()
        : juce::String();

    chineseProcessor.setUiLanguage(GravePitchUiLanguage::english);
    juce::Thread::sleep(40);
    juce::Timer::callPendingTimersSynchronously();
    ok &= expectTrue(chineseProcessor.uiLanguage() == GravePitchUiLanguage::english,
        "language preference changes only its processor instance");
    ok &= expectTrue(independentProcessor.uiLanguage() == GravePitchUiLanguage::simplifiedChinese,
        "another processor instance keeps its own language");
    ok &= expectTrue(tuningDrawerButton != nullptr && !tuningDrawerButton->isVisible(),
        "language switch keeps the tuning drawer open");
    ok &= expectTrue(saveButton != nullptr && saveButton->isVisible(),
        "language switch keeps drawer actions visible");
    ok &= expectTrue(draftStringEditor != nullptr
            && draftStringEditor->getText() == draftBeforeLanguageSwitch,
        "language switch preserves unsaved string editor draft");

    return ok;
}

bool testAboutOverlayContentAndBehaviour()
{
    GravePitchAudioProcessor processor;
    processor.setOutputMuted(false);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created for about overlay inspection");
    if (editor == nullptr) {
        return false;
    }

    auto* aboutOverlay = dynamic_cast<AboutOverlay*>(editor->findChildWithID("aboutOverlay"));
    auto* tuningDrawerButton = dynamic_cast<juce::TextButton*>(
        editor->findChildWithID("tuningDrawerButton"));
    auto* saveButton = dynamic_cast<juce::TextButton*>(
        editor->findChildWithID("saveCustomButton"));
    juce::ComboBox* draftStringEditor = nullptr;
    for (auto* child : editor->getChildren()) {
        if (auto* comboBox = dynamic_cast<juce::ComboBox*>(child);
            comboBox != nullptr && comboBox->getNumItems() == 53) {
            draftStringEditor = comboBox;
            break;
        }
    }

    ok &= expectTrue(aboutOverlay != nullptr, "editor owns a full-size about overlay");
    ok &= expectTrue(tuningDrawerButton != nullptr, "editor exposes the tuning drawer control");
    ok &= expectTrue(saveButton != nullptr, "editor exposes the drawer save action");
    ok &= expectTrue(draftStringEditor != nullptr, "editor exposes a draft string editor");
    if (aboutOverlay == nullptr) {
        return false;
    }

    ok &= expectTrue(!aboutOverlay->isVisible(), "about overlay is hidden by default");
    ok &= expectTrue(aboutOverlay->getBounds() == editor->getLocalBounds(),
        "about overlay fills the fixed editor");
    ok &= expectTrue(aboutOverlay->cardBounds() == juce::Rectangle<int>(220, 110, 480, 300),
        "about card uses the fixed centred geometry");
    bool interceptsOverlayClicks = false;
    bool allowsChildClicks = false;
    aboutOverlay->getInterceptsMouseClicks(interceptsOverlayClicks, allowsChildClicks);
    ok &= expectTrue(interceptsOverlayClicks && allowsChildClicks,
        "about overlay intercepts the editor while keeping its actions clickable");
    ok &= expectTrue(editor->getChildComponent(editor->getNumChildComponents() - 1) == aboutOverlay,
        "about overlay remains above every editor control");
    const auto aboutIcon = juce::ImageCache::getFromMemory(
        BinaryData::gravepitch_app_icon_2x_png,
        BinaryData::gravepitch_app_icon_2x_pngSize);
    ok &= expectTrue(aboutIcon.getWidth() == 128 && aboutIcon.getHeight() == 128,
        "about overlay uses the dedicated 128-pixel 2x app icon");
    ok &= expectTrue(aboutOverlay->projectNameText() == "GRAVEPITCH",
        "about overlay keeps the project name in English");
    ok &= expectTrue(aboutOverlay->versionText() == "Version " + juce::String(JucePlugin_VersionString),
        "about overlay reads the build version dynamically");
    ok &= expectTrue(
        aboutOverlay->descriptionText()
            == "A monophonic guitar tuner for guitarists, available as a standalone app and VST3 plugin.",
        "about overlay shows the approved English description");
    ok &= expectTrue(aboutOverlay->copyrightText()
            == juce::String::fromUTF8("© 2026 Pyromaniac6669"),
        "about overlay shows the approved copyright");
    ok &= expectTrue(aboutOverlay->licenseText() == "AGPL-3.0-or-later",
        "about overlay shows the license identifier");
    ok &= expectTrue(AboutOverlay::githubUrl()
            == "https://github.com/Pyromaniac6669/GravePitch",
        "about overlay uses the canonical GitHub URL");

    auto* githubButton = dynamic_cast<juce::TextButton*>(
        aboutOverlay->findChildWithID("aboutGithubButton"));
    auto* closeButton = dynamic_cast<juce::TextButton*>(
        aboutOverlay->findChildWithID("aboutCloseButton"));
    ok &= expectTrue(githubButton != nullptr && githubButton->getButtonText() == "GitHub",
        "about overlay exposes the GitHub action");
    ok &= expectTrue(githubButton != nullptr && githubButton->onClick != nullptr,
        "GitHub action is wired");
    ok &= expectTrue(closeButton != nullptr && closeButton->getButtonText() == "CLOSE",
        "English about overlay exposes the close action");

    if (tuningDrawerButton != nullptr && tuningDrawerButton->onClick != nullptr) {
        tuningDrawerButton->onClick();
    }
    if (draftStringEditor != nullptr) {
        draftStringEditor->setSelectedId(1, juce::sendNotificationSync);
    }
    const auto draftBeforeOpen = draftStringEditor != nullptr
        ? draftStringEditor->getText()
        : juce::String();
    const auto muteBeforeOpen = processor.outputMuted();
    const auto languageBeforeOpen = processor.uiLanguage();

    aboutOverlay->showOverlay();
    ok &= expectTrue(aboutOverlay->isVisible(), "about overlay can be opened");

    juce::Image aboutImage(
        juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics aboutGraphics(aboutImage);
    editor->paintEntireComponent(aboutGraphics, true);
    const auto iconBackingLuminance = averageLuminance(
        aboutImage, juce::Rectangle<int>(422, 141, 3, 14));
    const auto cardBackgroundLuminance = averageLuminance(
        aboutImage, juce::Rectangle<int>(408, 141, 3, 14));
    const auto hardBackingDifference = std::abs(
        iconBackingLuminance - cardBackgroundLuminance);
    ok &= expectTrue(
        hardBackingDifference < 8.0f,
        "about icon avoids a hard geometric backing");

    const auto scaledAboutIcon = aboutIcon.rescaled(
        72, 72, juce::Graphics::highResamplingQuality);
    double midtoneLift = 0.0;
    int opaquePixelCount = 0;
    for (int y = 0; y < scaledAboutIcon.getHeight(); ++y) {
        for (int x = 0; x < scaledAboutIcon.getWidth(); ++x) {
            const auto sourceColour = scaledAboutIcon.getPixelAt(x, y);
            if (sourceColour.getAlpha() < 192) {
                continue;
            }

            const auto renderedColour = aboutImage.getPixelAt(424 + x, 112 + y);
            midtoneLift += colourLuminance(renderedColour)
                - colourLuminance(sourceColour);
            ++opaquePixelCount;
        }
    }
    const auto averageMidtoneLift = opaquePixelCount > 0
        ? static_cast<float>(midtoneLift / opaquePixelCount)
        : 0.0f;
    ok &= expectTrue(
        averageMidtoneLift >= 15.0f,
        "about icon midtones are lifted without replacing its artwork");

    const auto outsideClickTime = juce::Time::getCurrentTime();
    const juce::MouseEvent outsideClick(
        juce::Desktop::getInstance().getMainMouseSource(),
        {10.0f, 10.0f},
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        aboutOverlay,
        aboutOverlay,
        outsideClickTime,
        {10.0f, 10.0f},
        outsideClickTime,
        1,
        false);
    aboutOverlay->mouseDown(outsideClick);
    ok &= expectTrue(aboutOverlay->isVisible(),
        "clicking outside the about card does not close it");
    ok &= expectTrue(saveButton != nullptr && saveButton->isVisible(),
        "opening about keeps the tuning drawer state");
    ok &= expectTrue(draftStringEditor != nullptr
            && draftStringEditor->getText() == draftBeforeOpen,
        "opening about preserves the unsaved tuning draft");
    ok &= expectTrue(processor.outputMuted() == muteBeforeOpen,
        "opening about preserves MUTE");
    ok &= expectTrue(processor.uiLanguage() == languageBeforeOpen,
        "opening about does not write language state");

    if (closeButton != nullptr && closeButton->onClick != nullptr) {
        closeButton->onClick();
    }
    ok &= expectTrue(!aboutOverlay->isVisible(), "close button hides the about overlay");
    ok &= expectTrue(saveButton != nullptr && saveButton->isVisible(),
        "closing about restores the unchanged tuning drawer");
    ok &= expectTrue(draftStringEditor != nullptr
            && draftStringEditor->getText() == draftBeforeOpen,
        "closing about preserves the unsaved tuning draft");

    aboutOverlay->showOverlay();
    ok &= expectTrue(
        aboutOverlay->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
        "about overlay handles Escape");
    ok &= expectTrue(!aboutOverlay->isVisible(), "Escape hides the about overlay");

    aboutOverlay->showOverlay();
    processor.setUiLanguage(GravePitchUiLanguage::simplifiedChinese);
    juce::Thread::sleep(40);
    juce::Timer::callPendingTimersSynchronously();
    ok &= expectTrue(aboutOverlay->isVisible(),
        "language switching keeps the about overlay open");
    ok &= expectTrue(aboutOverlay->projectNameText() == "GRAVEPITCH",
        "Chinese about overlay keeps the project name in English");
    ok &= expectTrue(
        aboutOverlay->versionText()
            == juce::String::fromUTF8("版本 ") + juce::String(JucePlugin_VersionString),
        "Chinese about overlay localizes the dynamic version label");
    ok &= expectTrue(
        aboutOverlay->descriptionText()
            == juce::String::fromUTF8("一款面向吉他手的单音调音器，支持独立运行和 VST3。"),
        "Chinese about overlay shows the approved description");
    ok &= expectTrue(closeButton != nullptr
            && closeButton->getButtonText() == juce::String::fromUTF8("关闭"),
        "Chinese about overlay localizes the close action");
    ok &= expectTrue(draftStringEditor != nullptr
            && draftStringEditor->getText() == draftBeforeOpen,
        "about language refresh preserves the unsaved tuning draft");
    ok &= expectTrue(processor.outputMuted() == muteBeforeOpen,
        "about language refresh preserves MUTE");
    aboutOverlay->hideOverlay();

    return ok;
}

bool testBundledCjkFontCoversTranslationCharacters()
{
    const juce::LocalisedStrings translations(
        juce::String::fromUTF8(
            reinterpret_cast<const char*>(BinaryData::zhHans_txt),
            BinaryData::zhHans_txtSize),
        false);
    const auto typeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::GravePitchCjkSubset_ttf,
        static_cast<std::size_t>(BinaryData::GravePitchCjkSubset_ttfSize));
    bool ok = expectTrue(translations.getLanguageName() == "Simplified Chinese",
        "simplified Chinese translation resource parses");
    ok &= expectTrue(translations.translate("TUNING CONFIGURATION") == juce::String::fromUTF8("调弦设置"),
        "translation resource contains the tuning configuration title");
    ok &= expectTrue(translations.translate("SAVE AS CUSTOM") == juce::String::fromUTF8("保存为自定义"),
        "translation resource contains the custom tuning action");
    ok &= expectTrue(translations.translate("About") == juce::String::fromUTF8("关于"),
        "translation resource contains the about menu action");
    ok &= expectTrue(
        translations.translate("Version ") == juce::String::fromUTF8("版本 "),
        "translation resource contains the version prefix");
    ok &= expectTrue(
        translations.translate(
            "A monophonic guitar tuner for guitarists, available as a standalone app and VST3 plugin.")
            == juce::String::fromUTF8("一款面向吉他手的单音调音器，支持独立运行和 VST3。"),
        "translation resource contains the about description");
    ok &= expectTrue(translations.translate("CLOSE") == juce::String::fromUTF8("关闭"),
        "translation resource contains the about close action");
    ok &= expectTrue(typeface != nullptr, "bundled CJK subset loads");
    if (typeface == nullptr) {
        return false;
    }

    auto translatedText = juce::String::fromUTF8("简体中文语言");
    for (const auto& value : translations.getMappings().getAllValues()) {
        translatedText += value;
    }
    for (const auto character : translatedText) {
        if (character > 0x7f) {
            ok &= expectTrue(typeface->getNominalGlyphForCodepoint(character).has_value(),
                "bundled CJK subset contains every translated character");
        }
    }

    return ok;
}

bool testPopupMenusKeepPreLocalizationFontMetrics()
{
    const auto oswaldTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::OswaldVariable_ttf,
        static_cast<std::size_t>(BinaryData::OswaldVariable_ttfSize));
    const auto cjkTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::GravePitchCjkSubset_ttf,
        static_cast<std::size_t>(BinaryData::GravePitchCjkSubset_ttfSize));
    GravePitchLookAndFeel localizedLookAndFeel;
    localizedLookAndFeel.setCjkFallbackTypeface(cjkTypeface);
    juce::LookAndFeel_V4 baselineLookAndFeel;

    const auto actualPopupFont = localizedLookAndFeel.getPopupMenuFont();
    const auto baselinePopupFont = baselineLookAndFeel.getPopupMenuFont();
    bool ok = expectTrue(actualPopupFont.getTypefaceName() == baselinePopupFont.getTypefaceName(),
        "popup menu keeps the pre-localization primary typeface");
    ok &= expectTrue(actualPopupFont.getTypefacePtr()->getName() == baselinePopupFont.getTypefacePtr()->getName(),
        "popup menu resolves to the pre-localization system typeface");
    ok &= expectTrue(actualPopupFont.getTypefacePtr()->getName() != oswaldTypeface->getName(),
        "popup menu does not force the condensed Oswald typeface");
    ok &= expectNear(actualPopupFont.getHeight(), baselinePopupFont.getHeight(),
        "popup menu keeps the pre-localization font height");
    ok &= expectNear(
        juce::GlyphArrangement::getStringWidth(actualPopupFont, "D Standard"),
        juce::GlyphArrangement::getStringWidth(baselinePopupFont, "D Standard"),
        "popup menu keeps the pre-localization English text width");
    ok &= expectTrue(actualPopupFont.getPreferredFallbackFamilies().contains(cjkTypeface->getName()),
        "popup menu adds the bundled CJK fallback");

    juce::ComboBox comboBox;
    comboBox.setSize(284, 32);
    const auto actualComboBoxFont = localizedLookAndFeel.getComboBoxFont(comboBox);
    const auto baselineComboBoxFont = baselineLookAndFeel.getComboBoxFont(comboBox);
    ok &= expectTrue(actualComboBoxFont.getTypefaceName() == baselineComboBoxFont.getTypefaceName(),
        "combo box keeps the pre-localization primary typeface");
    ok &= expectTrue(actualComboBoxFont.getTypefacePtr()->getName() == baselineComboBoxFont.getTypefacePtr()->getName(),
        "combo box resolves to the pre-localization system typeface");
    ok &= expectNear(actualComboBoxFont.getHeight(), baselineComboBoxFont.getHeight(),
        "combo box keeps the pre-localization font height");
    ok &= expectTrue(actualComboBoxFont.getPreferredFallbackFamilies().contains(cjkTypeface->getName()),
        "combo box adds the bundled CJK fallback");
    return ok;
}

bool testEditorRendersAtFixedSize()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created");
    if (editor == nullptr) {
        return false;
    }

    ok &= expectTrue(editor->getWidth() == 920 && editor->getHeight() == 520, "editor keeps the fixed design size");
    ok &= expectTrue(!editor->isResizable(), "editor rejects host resizing");

    juce::Image image(juce::Image::ARGB, 920, 520, true);
    juce::Graphics graphics(image);
    editor->paintEntireComponent(graphics, true);
    ok &= expectTrue(image.getPixelAt(4, 4).getAlpha() > 0, "editor paints an opaque background");
    return ok;
}

bool testCentsScaleIsLinearAndClamped()
{
    bool ok = true;
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(-50.0), 0.0f,
        "minus fifty cents maps to the left endpoint");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(-25.0), 0.25f,
        "minus twenty-five cents maps to one quarter");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(-10.0), 0.40f,
        "minus ten cents maps linearly");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(-5.0), 0.45f,
        "minus five cents maps to the left in-tune boundary");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(0.0), 0.50f,
        "zero cents maps to the centre");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(5.0), 0.55f,
        "plus five cents maps to the right in-tune boundary");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(10.0), 0.60f,
        "plus ten cents maps linearly");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(25.0), 0.75f,
        "plus twenty-five cents maps to three quarters");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(50.0), 1.0f,
        "plus fifty cents maps to the right endpoint");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(-80.0), 0.0f,
        "values below the scale clamp left");
    ok &= expectNear(GravePitchAudioProcessorEditor::scalePositionForCents(80.0), 1.0f,
        "values above the scale clamp right");
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
            ok &= expectTrue(component->getBounds() == juce::Rectangle<int>(822, 7, 64, 62),
                "in-tune indicator uses the enlarged proportional bounds");
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
            ok &= expectTrue(slider->getTextBoxPosition() == juce::Slider::NoTextBox,
                "A4 slider does not reserve a hidden value box");
            ok &= expectTrue(slider->getSliderSnapsToMousePosition(),
                "A4 slider clicks snap directly to the mouse position");
            ok &= expectTrue(std::abs(slider->getInterval() - 1.0) < 0.000001,
                "A4 slider uses whole-Hz steps");
            ok &= expectTrue(std::abs(slider->getX() + slider->getPositionOfValue(432.0) - 540.0f) < 0.000001f,
                "A4 minimum aligns with the visible track start");
            ok &= expectTrue(std::abs(slider->getX() + slider->getPositionOfValue(448.0) - 747.0f) < 0.000001f,
                "A4 maximum aligns with the visible track end");
        }
    }

    ok &= expectTrue(foundInTuneIndicator, "editor contains the in-tune indicator");
    ok &= expectTrue(foundA4Slider, "editor contains the A4 calibration slider");
    return ok;
}

bool testEditorUsesSharpOnlyNoteChoices()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created for note-choice inspection");
    if (editor == nullptr) {
        return false;
    }

    int stringEditorCount = 0;
    for (int componentIndex = 0; componentIndex < editor->getNumChildComponents(); ++componentIndex) {
        auto* comboBox = dynamic_cast<juce::ComboBox*>(editor->getChildComponent(componentIndex));
        if (comboBox == nullptr || comboBox->getNumItems() <= 20) {
            continue;
        }

        ++stringEditorCount;
        ok &= expectTrue(comboBox->getNumItems() == 53, "string editor contains one name per pitch");
        bool containsSharp = false;
        bool containsFlat = false;
        for (int itemIndex = 0; itemIndex < comboBox->getNumItems(); ++itemIndex) {
            const auto itemText = comboBox->getItemText(itemIndex);
            containsSharp = containsSharp || itemText.containsChar('#');
            containsFlat = containsFlat || itemText.containsChar('b');
        }
        ok &= expectTrue(containsSharp, "string editor exposes sharp note names");
        ok &= expectTrue(!containsFlat, "string editor omits enharmonic flat duplicates");
    }

    ok &= expectTrue(stringEditorCount == 6, "all six string editors use the canonical note list");
    return ok;
}

bool testPrimaryNoteNameOmitsOctave()
{
    return expectTrue(GravePitchAudioProcessorEditor::noteNameWithoutOctave("D4") == "D",
               "natural note omits the octave")
        && expectTrue(GravePitchAudioProcessorEditor::noteNameWithoutOctave("C#4") == "C#",
            "sharp note omits the octave")
        && expectTrue(GravePitchAudioProcessorEditor::noteNameWithoutOctave("C-1") == "C",
            "negative octave is removed as a unit");
}

bool testA4UsesIntegerSliderOnly()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created for integer A4 inspection");
    if (editor == nullptr) {
        return false;
    }

    juce::Slider* a4Slider = nullptr;
    bool foundTextEditor = false;
    for (int componentIndex = 0; componentIndex < editor->getNumChildComponents(); ++componentIndex) {
        auto* component = editor->getChildComponent(componentIndex);
        a4Slider = a4Slider != nullptr ? a4Slider : dynamic_cast<juce::Slider*>(component);
        foundTextEditor = foundTextEditor || dynamic_cast<juce::TextEditor*>(component) != nullptr;
    }

    ok &= expectTrue(!foundTextEditor, "A4 calibration does not expose manual text entry");
    ok &= expectTrue(a4Slider != nullptr, "A4 calibration keeps the slider");
    if (a4Slider == nullptr) {
        return false;
    }

    a4Slider->setValue(442.6, juce::sendNotificationSync);
    ok &= expectTrue(std::abs(a4Slider->getValue() - 443.0) < 0.000001,
        "A4 slider snaps fractional input to the nearest whole Hz");
    ok &= expectTrue(std::abs(processor.a4Hz() - 443.0) < 0.000001,
        "A4 slider applies the snapped whole-Hz value");
    return ok;
}

bool testDrawerDoesNotRelayoutMainInterface()
{
    GravePitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    bool ok = expectTrue(editor != nullptr, "editor is created for drawer overlay inspection");
    if (editor == nullptr) {
        return false;
    }

    auto render = [editorPointer = editor.get()] {
        juce::Image image(juce::Image::ARGB, editorPointer->getWidth(), editorPointer->getHeight(), true);
        juce::Graphics graphics(image);
        editorPointer->paintEntireComponent(graphics, true);
        return image;
    };

    const auto collapsed = render();
    for (int componentIndex = 0; componentIndex < editor->getNumChildComponents(); ++componentIndex) {
        if (auto* button = dynamic_cast<juce::TextButton*>(editor->getChildComponent(componentIndex))) {
            if (button->getButtonText().contains("6 STRING") && button->onClick != nullptr) {
                button->onClick();
                break;
            }
        }
    }
    const auto expanded = render();

    for (int y = 0; y < 285; ++y) {
        for (int x = 0; x < editor->getWidth(); ++x) {
            if (collapsed.getPixelAt(x, y) != expanded.getPixelAt(x, y)) {
                return expectTrue(false, "drawer overlay keeps the upper main interface pixel-identical");
            }
        }
    }

    int changedPixels = 0;
    for (int y = 292; y < editor->getHeight(); ++y) {
        for (int x = 0; x < editor->getWidth(); ++x) {
            changedPixels += collapsed.getPixelAt(x, y) != expanded.getPixelAt(x, y) ? 1 : 0;
        }
    }
    ok &= expectTrue(changedPixels > 1000, "drawer is painted as a visible lower overlay");
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

bool renderEditorReference(
    GravePitchUiLanguage language,
    const char* collapsedPath,
    const char* expandedPath)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    GravePitchAudioProcessor processor;
    processor.setTuningIndex(2);
    processor.setUiLanguage(language);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int block = 0; block < 48; ++block) {
        fillStereoSine(buffer, phase, 294.174, sampleRate);
        processor.processBlock(buffer, midi);
        juce::Thread::sleep(1);
    }
    for (int attempt = 0; attempt < 100 && !processor.snapshot().stable; ++attempt) {
        juce::Thread::sleep(5);
    }

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
            if (button->getComponentID() == "tuningDrawerButton" && button->onClick != nullptr) {
                button->onClick();
                break;
            }
        }
    }

    return writeImage(expandedPath);
}

bool renderAboutReference(GravePitchUiLanguage language, const char* outputPath)
{
    GravePitchAudioProcessor processor;
    processor.setTuningIndex(2);
    processor.setUiLanguage(language);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr) {
        return false;
    }

    auto* aboutOverlay = dynamic_cast<AboutOverlay*>(editor->findChildWithID("aboutOverlay"));
    if (aboutOverlay == nullptr) {
        return false;
    }
    aboutOverlay->showOverlay();

    juce::Image warmupImage(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics warmupGraphics(warmupImage);
    editor->paintEntireComponent(warmupGraphics, true);
    juce::Thread::sleep(30);

    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics graphics(image);
    editor->paintEntireComponent(graphics, true);
    auto stream = juce::File(outputPath).createOutputStream();
    return stream != nullptr && juce::PNGImageFormat().writeImageToStream(image, *stream);
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
    ok &= testUiLanguageStateRoundTripsAndDefaultsSafely();
    ok &= testSimplifiedChineseEditorTextAndInstanceIsolation();
    ok &= testAboutOverlayContentAndBehaviour();
    ok &= testBundledCjkFontCoversTranslationCharacters();
    ok &= testPopupMenusKeepPreLocalizationFontMetrics();
    ok &= testEditorRendersAtFixedSize();
    ok &= testCentsScaleIsLinearAndClamped();
    ok &= testEditorKeepsOnlyActionableReadouts();
    ok &= testEditorUsesSharpOnlyNoteChoices();
    ok &= testPrimaryNoteNameOmitsOctave();
    ok &= testA4UsesIntegerSliderOnly();
    ok &= testDrawerDoesNotRelayoutMainInterface();
    ok &= testInTuneIndicatorState();

    if (ok && (argc == 5 || argc == 7)) {
        ok &= expectTrue(
            renderEditorReference(GravePitchUiLanguage::english, argv[1], argv[2]),
            "English editor reference images are written");
        ok &= expectTrue(
            renderEditorReference(GravePitchUiLanguage::simplifiedChinese, argv[3], argv[4]),
            "simplified Chinese editor reference images are written");
        if (argc == 7) {
            ok &= expectTrue(
                renderAboutReference(GravePitchUiLanguage::english, argv[5]),
                "English about reference image is written");
            ok &= expectTrue(
                renderAboutReference(GravePitchUiLanguage::simplifiedChinese, argv[6]),
                "simplified Chinese about reference image is written");
        }
    }

    if (!ok) {
        return 1;
    }

    std::cout << "Plugin processor tests passed\n";
    return 0;
}
