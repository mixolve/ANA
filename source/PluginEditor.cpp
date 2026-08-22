#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "UiStyle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;

juce::String formatFrequency(const double frequency)
{
    if (frequency >= 1000.0)
        return juce::String(frequency / 1000.0, frequency >= 10000.0 ? 1 : 2) + " KHZ";

    return juce::String(frequency, frequency >= 100.0 ? 0 : 1) + " HZ";
}

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}

} // namespace

AnaScopeButton::AnaScopeButton(juce::String text)
    : juce::Button(std::move(text))
{
    setWantsKeyboardFocus(false);
}

void AnaScopeButton::paintButton(juce::Graphics& graphics,
                                 const bool shouldDrawButtonAsHighlighted,
                                 const bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto fill = shouldDrawButtonAsDown ? ana::ui::grey700 : ana::ui::grey800;

    if (shouldDrawButtonAsHighlighted && isEnabled())
        fill = fill.brighter(0.08f);

    graphics.setColour(fill);
    graphics.fillRect(bounds);
    graphics.setColour(getToggleState() ? ana::ui::accent : ana::ui::grey500);
    graphics.drawRect(bounds, getToggleState() ? 1.5f : 1.0f);
    graphics.setColour(isEnabled() ? ana::ui::white : ana::ui::grey500);
    graphics.setFont(ana::ui::makeFont(13.0f, getToggleState()));
    graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(7, 1),
                            juce::Justification::centred, 1);
}

void AnaSliderLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                            const int x, const int y, const int width, const int height,
                                            const float sliderPosition,
                                            const float minimumSliderPosition,
                                            const float maximumSliderPosition,
                                            const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height,
                                               sliderPosition, minimumSliderPosition, maximumSliderPosition,
                                               style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), sliderPosition);

    graphics.setColour(ana::ui::grey800);
    graphics.fillRect(bounds);
    graphics.setColour(ana::ui::accent.withAlpha(slider.isEnabled() ? 0.18f : 0.06f));
    graphics.fillRect(bounds.withRight(markerX));
    graphics.setColour(slider.isEnabled() ? ana::ui::accent : ana::ui::grey500);
    graphics.fillRect(markerX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(bounds, 1.0f);

    if (style == juce::Slider::LinearBar)
    {
        graphics.setColour(slider.isEnabled() ? ana::ui::white : ana::ui::grey500);
        graphics.setFont(ana::ui::makeFont(13.0f, false));
        graphics.drawFittedText(slider.getTextFromValue(slider.getValue()),
                                bounds.toNearestInt().reduced(10, 1),
                                juce::Justification::centred, 1);
    }
}

juce::Label* AnaSliderLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(ana::ui::makeFont(13.0f, false));
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::backgroundColourId, ana::ui::grey800);
    label->setColour(juce::Label::outlineColourId, ana::ui::grey500);
    label->setColour(juce::Label::textColourId, ana::ui::white);
    label->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
    return label;
}

AnaParameterControl::AnaParameterControl(juce::AudioProcessorValueTreeState& state,
                                         const juce::String& parameterId,
                                         juce::String title,
                                         Formatter formatter)
    : titleText(std::move(title)), valueFormatter(std::move(formatter)), compact(titleText.isEmpty())
{
    slider.setLookAndFeel(&sliderLookAndFeel);
    slider.setSliderStyle(compact ? juce::Slider::LinearBar : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                           false, compact ? 0 : 116, 34);
    slider.setScrollWheelEnabled(false);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
    slider.setColour(juce::Slider::textBoxTextColourId, ana::ui::white);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, ana::ui::grey800);
    slider.setColour(juce::Slider::textBoxOutlineColourId, ana::ui::grey500);
    addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, parameterId, slider);
}

AnaParameterControl::~AnaParameterControl()
{
    slider.setLookAndFeel(nullptr);
}

void AnaParameterControl::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;
    slider.setVisible(interactionEnabled);
    repaint();
}

void AnaParameterControl::paint(juce::Graphics& graphics)
{
    if (! compact)
    {
        graphics.setColour(ana::ui::grey800);
        graphics.fillRect(titleBounds);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(titleBounds);
        graphics.setColour(interactionEnabled ? ana::ui::white : ana::ui::grey500);
        graphics.setFont(ana::ui::makeFont(13.0f, false));
        graphics.drawFittedText(titleText, titleBounds.reduced(12, 1), juce::Justification::centredLeft, 1);
    }

    if (! interactionEnabled)
    {
        graphics.setColour(ana::ui::grey800);
        graphics.fillRect(valueBounds);
        graphics.setColour(ana::ui::grey500);
        graphics.drawRect(valueBounds);
        graphics.setFont(ana::ui::makeFont(13.0f, false));
        graphics.drawFittedText("OFF", valueBounds, juce::Justification::centred, 1);
    }
}

void AnaParameterControl::resized()
{
    auto row = getLocalBounds();

    if (! compact)
    {
        titleBounds = row.removeFromLeft(std::min(240, juce::roundToInt(static_cast<float>(row.getWidth()) * 0.36f)));
        row.removeFromLeft(std::min(8, row.getWidth()));
    }
    else
    {
        titleBounds = {};
    }

    valueBounds = row;
    slider.setBounds(valueBounds);
}

AnaMultibandScopeComponent::AnaMultibandScopeComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(true);

    constexpr std::array<const char*, 4> modeNames { "LEFT", "RIGHT", "MID", "SIDE" };

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto button = std::make_unique<AnaScopeButton>(modeNames[modeIndex]);
            button->onClick = [this, bandIndex, modeIndex]
            {
                const auto mode = static_cast<ana::ScopeChannelMode>(modeIndex);

                if (processor.getScopeChannelMode(bandIndex) != mode)
                {
                    processor.setScopeChannelMode(bandIndex, mode);
                    historyChannelModes[bandIndex] = mode;
                    columnMinimums[bandIndex] = std::numeric_limits<float>::max();
                    columnMaximums[bandIndex] = std::numeric_limits<float>::lowest();
                }

                refreshBandModeButtons();
            };
            addAndMakeVisible(*button);
            bandModeButtons[bandIndex][modeIndex] = std::move(button);
        }

        auto clearButton = std::make_unique<AnaScopeButton>("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);
    }

    resetColumnAccumulator();
    refreshBandModeButtons();
    startTimerHz(60);
}

void AnaMultibandScopeComponent::setFrozen(const bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;

    if (! frozen)
    {
        readCursor = processor.getMultibandScope().getWriteCursor();
        columnSampleProgress = 0.0;
        resetColumnAccumulator();
    }
}

void AnaMultibandScopeComponent::clearHistory()
{
    resetHistory();
}

void AnaMultibandScopeComponent::timerCallback()
{
    if (frozen)
        return;

    const auto timeMilliseconds = processor.getScopeTimeMilliseconds();
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> currentModes;

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

        if (historyChannelModes[bandIndex] != currentModes[bandIndex])
        {
            historyChannelModes[bandIndex] = currentModes[bandIndex];
            columnMinimums[bandIndex] = std::numeric_limits<float>::max();
            columnMaximums[bandIndex] = std::numeric_limits<float>::lowest();
        }
    }

    refreshBandModeButtons();

    if (historyImage.getWidth() != getWidth()
        || historyImage.getHeight() != getHeight()
        || std::abs(historyTimeMilliseconds - timeMilliseconds) > 0.001
        || historyBandCount != activeBandCount)
    {
        resetHistory();
        return;
    }

    if (! historyImage.isValid() || getWidth() <= 0)
        return;

    auto& scope = processor.getMultibandScope();
    scope.copySince(incomingSamples, readCursor);

    const auto sampleCount = incomingSamples.front().front().size();
    if (sampleCount == 0)
        return;

    const auto samplesPerColumn = std::max(
        0.001,
        scope.getSampleRate() * timeMilliseconds * 0.001 / static_cast<double>(getWidth()));
    auto appendedColumn = false;

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        {
            const auto sample = getDisplayedSample(bandIndex, sampleIndex);
            columnMinimums[bandIndex] = std::min(columnMinimums[bandIndex], sample);
            columnMaximums[bandIndex] = std::max(columnMaximums[bandIndex], sample);
        }

        columnSampleProgress += 1.0;
        const auto columnsToAppend = static_cast<size_t>(columnSampleProgress / samplesPerColumn);

        if (columnsToAppend == 0)
            continue;

        for (size_t columnIndex = 0; columnIndex < columnsToAppend; ++columnIndex)
            appendHistoryColumn(activeBandCount);

        columnSampleProgress -= static_cast<double>(columnsToAppend) * samplesPerColumn;
        resetColumnAccumulator();
        appendedColumn = true;
    }

    if (appendedColumn)
        repaint();
}

void AnaMultibandScopeComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::grey800);

    if (historyImage.isValid())
        graphics.drawImageAt(historyImage, 0, 0);

    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    graphics.setColour(ana::ui::white);

    for (size_t bandIndex = 1; bandIndex < activeBandCount; ++bandIndex)
    {
        const auto y = juce::roundToInt(static_cast<double>(getHeight()) * static_cast<double>(bandIndex)
                                        / static_cast<double>(activeBandCount));
        graphics.fillRect(0, y, getWidth(), 1);
    }
}

void AnaMultibandScopeComponent::resized()
{
    refreshBandModeButtons();
    resetHistory();
}

void AnaMultibandScopeComponent::resetHistory()
{
    historyTimeMilliseconds = processor.getScopeTimeMilliseconds();
    historyBandCount = processor.getActiveSplitCount() + 1;

    for (size_t bandIndex = 0; bandIndex < historyChannelModes.size(); ++bandIndex)
        historyChannelModes[bandIndex] = processor.getScopeChannelMode(bandIndex);

    readCursor = processor.getMultibandScope().getWriteCursor();
    columnSampleProgress = 0.0;
    resetColumnAccumulator();

    if (getWidth() <= 0 || getHeight() <= 0)
    {
        historyImage = {};
        return;
    }

    historyImage = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics imageGraphics(historyImage);
    imageGraphics.fillAll(ana::ui::grey800);
    repaint();
}

void AnaMultibandScopeComponent::clearBandHistory(const size_t bandIndex)
{
    if (! historyImage.isValid() || bandIndex >= historyBandCount)
        return;

    const auto top = juce::roundToInt(static_cast<double>(historyImage.getHeight())
                                      * static_cast<double>(bandIndex)
                                      / static_cast<double>(historyBandCount));
    const auto bottom = juce::roundToInt(static_cast<double>(historyImage.getHeight())
                                         * static_cast<double>(bandIndex + 1)
                                         / static_cast<double>(historyBandCount));
    juce::Graphics graphics(historyImage);
    graphics.setColour(ana::ui::grey800);
    graphics.fillRect(0, top, historyImage.getWidth(), std::max(0, bottom - top));
    columnMinimums[bandIndex] = std::numeric_limits<float>::max();
    columnMaximums[bandIndex] = std::numeric_limits<float>::lowest();
    repaint(0, top, getWidth(), std::max(0, bottom - top));
}

void AnaMultibandScopeComponent::resetColumnAccumulator()
{
    columnMinimums.fill(std::numeric_limits<float>::max());
    columnMaximums.fill(std::numeric_limits<float>::lowest());
}

void AnaMultibandScopeComponent::refreshBandModeButtons()
{
    const auto activeBandCount = processor.getActiveSplitCount() + 1;
    const auto laneHeight = getHeight() > 0
        ? static_cast<float>(getHeight()) / static_cast<float>(activeBandCount)
        : 0.0f;
    constexpr int buttonWidth = 52;
    constexpr int buttonHeight = 22;
    constexpr int buttonGap = 4;

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        const auto isActive = bandIndex < activeBandCount;
        const auto selectedMode = processor.getScopeChannelMode(bandIndex);

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isActive);
            button.setToggleState(static_cast<size_t>(selectedMode) == modeIndex, juce::dontSendNotification);

            if (isActive)
            {
                const auto x = 8 + static_cast<int>(modeIndex) * (buttonWidth + buttonGap);
                const auto y = juce::roundToInt(laneHeight * static_cast<float>(bandIndex)) + 7;
                button.setBounds(x, y, buttonWidth, buttonHeight);
            }
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isActive);

        if (isActive)
        {
            const auto x = 8 + static_cast<int>(bandModeButtons[bandIndex].size())
                               * (buttonWidth + buttonGap);
            const auto y = juce::roundToInt(laneHeight * static_cast<float>(bandIndex)) + 7;
            clearButton.setBounds(x, y, buttonWidth, buttonHeight);
        }
    }
}

float AnaMultibandScopeComponent::getDisplayedSample(const size_t bandIndex,
                                                     const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];

    switch (historyChannelModes[bandIndex])
    {
        case ana::ScopeChannelMode::left:  return left;
        case ana::ScopeChannelMode::right: return right;
        case ana::ScopeChannelMode::side:  return 0.5f * (left - right);
        case ana::ScopeChannelMode::mid:   return 0.5f * (left + right);
    }

    return 0.0f;
}

void AnaMultibandScopeComponent::appendHistoryColumn(const size_t activeBandCount)
{
    if (! historyImage.isValid())
        return;

    const auto width = historyImage.getWidth();
    const auto height = historyImage.getHeight();

    if (width > 1)
        historyImage.moveImageSection(0, 0, 1, 0, width - 1, height);

    juce::Graphics graphics(historyImage);
    graphics.setColour(ana::ui::grey800);
    graphics.fillRect(width - 1, 0, 1, height);
    graphics.setColour(ana::ui::white);

    const auto laneHeight = static_cast<float>(height) / static_cast<float>(activeBandCount);

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        if (columnMinimums[bandIndex] > columnMaximums[bandIndex])
            continue;

        const auto centreY = laneHeight * (static_cast<float>(bandIndex) + 0.5f);
        const auto amplitude = std::max(0.0f, (laneHeight - 14.0f) * 0.46f);
        const auto minimum = juce::jlimit(-1.0f, 1.0f, columnMinimums[bandIndex]);
        const auto maximum = juce::jlimit(-1.0f, 1.0f, columnMaximums[bandIndex]);
        const auto top = centreY - maximum * amplitude;
        const auto bottom = centreY - minimum * amplitude;
        const auto topPixel = juce::jlimit(0, height - 1, juce::roundToInt(std::min(top, bottom)));
        const auto bottomPixel = juce::jlimit(0, height - 1, juce::roundToInt(std::max(top, bottom)));
        graphics.fillRect(width - 1, topPixel, 1, std::max(1, bottomPixel - topPixel + 1));
    }
}

AnaCrossoverSettingsComponent::AnaCrossoverSettingsComponent(AnaAudioProcessor& processorRef)
    : processor(processorRef)
{
    addAndMakeVisible(addCrossoverButton);
    addAndMakeVisible(removeCrossoverButton);
    addCrossoverButton.onClick = [this] { changeActiveSplitCount(1); };
    removeCrossoverButton.onClick = [this] { changeActiveSplitCount(-1); };

    for (size_t index = 0; index < crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<AnaParameterControl>(
            processor.getParameters(),
            AnaAudioProcessor::crossoverParameterIds[index],
            "XOVER-" + juce::String(static_cast<int>(index + 1)),
            [] (const double value) { return formatFrequency(value); });
        control->getSlider().valueFromTextFunction = [] (const juce::String& text)
        {
            return parseFrequency(text);
        };
        control->getSlider().onValueChange = [this, index] { constrainFrequency(index); };
        addAndMakeVisible(*control);
        crossoverControls[index] = std::move(control);
    }

    for (size_t index = 0; index < processor.getActiveSplitCount(); ++index)
        constrainFrequency(index);

    refreshExternalState();
    startTimerHz(15);
}

void AnaCrossoverSettingsComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::grey800);
    auto heading = getLocalBounds().reduced(16).removeFromTop(34);
    graphics.setColour(ana::ui::grey500);
    graphics.drawRect(heading);
    graphics.setFont(ana::ui::makeFont(13.0f, true));
    graphics.drawFittedText("CROSSOVER SETTINGS", heading.reduced(12, 1),
                            juce::Justification::centredLeft, 1);
}

void AnaCrossoverSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(42);
    constexpr int rowHeight = 34;
    constexpr int gap = 8;

    addCrossoverButton.setBounds(area.removeFromTop(rowHeight));
    area.removeFromTop(gap);

    for (auto& control : crossoverControls)
    {
        control->setBounds(area.removeFromTop(rowHeight));
        area.removeFromTop(gap);
    }

    removeCrossoverButton.setBounds(area.removeFromTop(rowHeight));
}

void AnaCrossoverSettingsComponent::timerCallback()
{
    refreshExternalState();
}

void AnaCrossoverSettingsComponent::changeActiveSplitCount(const int delta)
{
    const auto currentCount = static_cast<int>(processor.getActiveSplitCount());
    const auto newCount = juce::jlimit(0, static_cast<int>(crossoverControls.size()), currentCount + delta);

    if (newCount != currentCount)
    {
        processor.setActiveSplitCount(static_cast<size_t>(newCount));

        for (int index = 0; index < newCount; ++index)
            constrainFrequency(static_cast<size_t>(index));
    }

    refreshExternalState();
}

void AnaCrossoverSettingsComponent::constrainFrequency(const size_t crossoverIndex)
{
    if (constrainingFrequency || crossoverIndex >= processor.getActiveSplitCount())
        return;

    const juce::ScopedValueSetter<bool> guard(constrainingFrequency, true);
    const auto activeSplitCount = processor.getActiveSplitCount();
    auto& slider = crossoverControls[crossoverIndex]->getSlider();
    auto lowerBound = slider.getMinimum();
    auto upperBound = slider.getMaximum();

    if (crossoverIndex > 0)
        lowerBound = crossoverControls[crossoverIndex - 1]->getSlider().getValue() + minimumCrossoverGapHz;

    if (crossoverIndex + 1 < activeSplitCount)
        upperBound = crossoverControls[crossoverIndex + 1]->getSlider().getValue() - minimumCrossoverGapHz;

    slider.setValue(juce::jlimit(lowerBound, std::max(lowerBound, upperBound), slider.getValue()),
                    juce::sendNotificationSync);
}

void AnaCrossoverSettingsComponent::refreshExternalState()
{
    const auto activeSplitCount = processor.getActiveSplitCount();
    addCrossoverButton.setEnabled(activeSplitCount < crossoverControls.size());
    removeCrossoverButton.setEnabled(activeSplitCount > 0);

    for (size_t index = 0; index < crossoverControls.size(); ++index)
        crossoverControls[index]->setInteractionEnabled(index < activeSplitCount);
}

AnaAudioProcessorEditor::AnaAudioProcessorEditor(AnaAudioProcessor& processorRef)
    : AudioProcessorEditor(&processorRef),
      audioProcessor(processorRef),
      scopeDisplay(processorRef),
      crossoverSettings(processorRef),
      timeControl(processorRef.getParameters(), AnaAudioProcessor::scopeTimeParameterId, {},
                  [] (const double value)
                  {
                      const auto seconds = value / 1000.0;
                      return juce::String(seconds, seconds < 10.0 ? 1 : 0) + " S";
                  })
{
    for (auto* component : std::array<juce::Component*, 9> {
             &scopeDisplay, &crossoverSettings, &timeControl, &frequencyButton, &phaseButton,
             &scopeButton, &settingsButton, &clearButton, &freezeButton })
        addAndMakeVisible(*component);

    frequencyButton.setEnabled(false);
    phaseButton.setEnabled(false);
    scopeButton.onClick = [this] { showCrossoverSettings(false); };
    settingsButton.onClick = [this] { showCrossoverSettings(true); };

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this] { scopeDisplay.setFrozen(freezeButton.getToggleState()); };
    clearButton.onClick = [this] { scopeDisplay.clearHistory(); };

    showCrossoverSettings(false);
    setResizable(true, true);
    setResizeLimits(720, 520, 1600, 1100);
    setSize(1024, 720);
}

void AnaAudioProcessorEditor::showCrossoverSettings(const bool shouldShowSettings)
{
    showingCrossoverSettings = shouldShowSettings;
    scopeButton.setToggleState(! shouldShowSettings, juce::dontSendNotification);
    settingsButton.setToggleState(shouldShowSettings, juce::dontSendNotification);
    scopeDisplay.setVisible(! shouldShowSettings);
    timeControl.setVisible(! shouldShowSettings);
    clearButton.setVisible(! shouldShowSettings);
    freezeButton.setVisible(! shouldShowSettings);
    crossoverSettings.setVisible(shouldShowSettings);
    resized();
    repaint();
}

void AnaAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::grey800.darker(0.35f));
}

void AnaAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto controlsRow = area.removeFromTop(34);
    auto modeArea = controlsRow.removeFromLeft(
        std::min(330, juce::roundToInt(static_cast<float>(controlsRow.getWidth()) * 0.44f)));
    frequencyButton.setBounds(modeArea.removeFromLeft(modeArea.getWidth() / 3));
    phaseButton.setBounds(modeArea.removeFromLeft(modeArea.getWidth() / 2));
    scopeButton.setBounds(modeArea);

    settingsButton.setBounds(controlsRow.removeFromRight(std::min(116, controlsRow.getWidth())));

    if (! showingCrossoverSettings)
    {
        controlsRow.removeFromRight(std::min(8, controlsRow.getWidth()));
        freezeButton.setBounds(controlsRow.removeFromRight(std::min(96, controlsRow.getWidth())));
        controlsRow.removeFromRight(std::min(8, controlsRow.getWidth()));
        clearButton.setBounds(controlsRow.removeFromRight(std::min(72, controlsRow.getWidth())));
        controlsRow.removeFromRight(std::min(8, controlsRow.getWidth()));
        auto timeArea = controlsRow.removeFromRight(std::min(220, controlsRow.getWidth()));
        timeControl.setBounds(timeArea);
    }

    area.removeFromTop(8);
    scopeDisplay.setBounds(area);
    crossoverSettings.setBounds(area);
}
