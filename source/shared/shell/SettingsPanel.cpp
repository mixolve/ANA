#include "SettingsPanel.h"
#include "Processor.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;

juce::String formatFrequency(const double frequency)
{
    return juce::String(frequency, frequency >= 100.0 ? 0 : 1);
}





double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}
}

SettingsPanel::SettingsPanel(PluginProcessor& processorRef)
    : processor(processorRef),
      scopSettings(processorRef),
      specSettings(processorRef),
      corrSettings(processorRef),
      lvlsSettings(processorRef)
{
    setOpaque(true);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(false, false, true, false);
    settingsViewport.setLookAndFeel(&focusedControlLookAndFeel);
    settingsViewport.setWantsKeyboardFocus(false);
    settingsViewport.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(settingsViewport);

    const auto configureHeading = [this] (EllipsisLabel& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(ana::ui::makeFont());
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, ana::ui::white);
        label.setDrawBackground(false);
        label.setColour(juce::Label::outlineColourId, ana::ui::light);
        label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
        label.setInterceptsMouseClicks(false, false);
        settingsContent.addAndMakeVisible(label);
    };
    configureHeading(generalHeadingLabel, "SPEC FREQ MAIN");
    configureHeading(controlsVisibilityHeadingLabel, "VIEW");
    configureHeading(scopMainHeadingLabel, "SCOP MAIN");

    for (auto* component : std::array<juce::Component*, 62> {
             &scopSettings.addCrossoverButton, &scopSettings.removeCrossoverButton, &scopSettings.equalHeightButton,
             &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.zoomControlsButton,
             &scopSettings.monitorControlsButton, &scopSettings.toolsButton, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl, &scopSettings.leftToRightButton,
             &specSettings.fftSizeControl,
             &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.averageTimeControl, &specSettings.smoothingControl,
             &specSettings.frequencyScaleControl,
             &specSettings.filledDisplayButton, &specSettings.secondGraphButton,
             &specSettings.firstGraphTypeControl, &specSettings.secondGraphTypeControl,
             &specSettings.antiAliasButton, &specSettings.highQualityRenderingButton, &specSettings.mapLeftToRightButton,
             &specSettings.slopeControl,
             &specSettings.rangeLowControl, &specSettings.rangeHighControl,
             &specSettings.clearOnPlayButton, &specSettings.rangesButton, &specSettings.cursorButton,
             &specSettings.monitorControlsButton, &specSettings.zoomControlsButton,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl,
             &corrSettings.secondGraphTypeControl,
             &corrSettings.filledDisplayButton, &corrSettings.secondGraphButton, &corrSettings.clearOnPlayButton,
             &corrSettings.rangesButton, &corrSettings.cursorButton,
             &corrSettings.zoomControlsButton, &lvlsSettings.widthControl,
             &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.centerSectionsButton,
             &lvlsSettings.peakRmsVisibleButton, &lvlsSettings.loudnessVisibleButton,
             &lvlsSettings.historyVisibleButton, &lvlsSettings.historyMomentaryButton,
             &lvlsSettings.historyShortTermButton, &lvlsSettings.historyIntegratedButton,
             &lvlsSettings.historyZoomButton })
        settingsContent.addAndMakeVisible(*component);

    addAndMakeVisible(focusedParameterControl);
    closeButton.setTooltip("CLOSE");
    closeButton.onClick = [this]
    {
        dismissParameterEditors();
        clearFocusedParameterControl();
        if (onCloseRequested)
            onCloseRequested();
    };
    addAndMakeVisible(closeButton);
    scopSettings.addCrossoverButton.onClick = [this] { changeCrossoverCount(1); };
    scopSettings.removeCrossoverButton.onClick = [this] { changeCrossoverCount(-1); };
    scopSettings.equalHeightButton.onClick = [this]
    {
        if (onEqualBandHeights)
            onEqualBandHeights();
    };
    lvlsSettings.centerSectionsButton.onClick = [this]
    {
        if (onCenterLvlsSections)
            onCenterLvlsSections();
    };
    const auto displaySettingChanged = [this]
    {
        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    scopSettings.styleControl.onValueChanged = displaySettingChanged;
    scopSettings.opacityControl.onValueChanged = displaySettingChanged;
    for (auto* button : std::array<ControlButton*, 3> {
             &scopSettings.zoomControlsButton, &scopSettings.monitorControlsButton, &scopSettings.toolsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    scopSettings.zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopZoomControlsParameterId, scopSettings.zoomControlsButton);
    scopSettings.monitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopMonitorControlsParameterId, scopSettings.monitorControlsButton);
    scopSettings.toolsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopToolsParameterId, scopSettings.toolsButton);
   #if ANA_VARIANT_RT
    scopSettings.leftToRightButton.setClickingTogglesState(true);
    scopSettings.leftToRightButton.onClick = displaySettingChanged;
    scopSettings.leftToRightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopLeftToRightParameterId,
        scopSettings.leftToRightButton);
    scopSettings.leftToRightButton.setTooltip(
        "Draw RT SCOP from left to right; clear and restart at the left edge after reaching the right edge");
   #endif
    for (auto* button : std::array<ControlButton*, 10> {
             &specSettings.filledDisplayButton, &specSettings.secondGraphButton,
             &specSettings.antiAliasButton, &specSettings.highQualityRenderingButton,
             &specSettings.mapLeftToRightButton, &specSettings.clearOnPlayButton, &specSettings.rangesButton,
             &specSettings.cursorButton, &specSettings.monitorControlsButton, &specSettings.zoomControlsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    specSettings.filledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specFilledDisplayParameterId, specSettings.filledDisplayButton);
    specSettings.secondGraphAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specSecondGraphParameterId, specSettings.secondGraphButton);
    specSettings.antiAliasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specAntiAliasParameterId, specSettings.antiAliasButton);
    specSettings.highQualityRenderingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specHighQualityRenderingParameterId, specSettings.highQualityRenderingButton);
    specSettings.highQualityRenderingButton.setTooltip(
        "Accurate max-bilinear interpolation of a spectrogram (recommended)");
    specSettings.mapLeftToRightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specMapLeftToRightParameterId, specSettings.mapLeftToRightButton);
    specSettings.mapLeftToRightButton.setTooltip(
        "Draw rt SPEC MAP from left to right; clear and restart at the left edge after reaching the right edge");
    specSettings.rangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specRangesVisibleParameterId, specSettings.rangesButton);
    specSettings.clearOnPlayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specClearOnPlayParameterId, specSettings.clearOnPlayButton);
    specSettings.cursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specCursorReadoutParameterId, specSettings.cursorButton);
    specSettings.monitorControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specMonitorControlsParameterId, specSettings.monitorControlsButton);
    specSettings.zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::specZoomControlsParameterId, specSettings.zoomControlsButton);
    for (auto* button : std::array<ControlButton*, 6> {
        &corrSettings.filledDisplayButton, &corrSettings.clearOnPlayButton, &corrSettings.rangesButton,
        &corrSettings.cursorButton, &corrSettings.zoomControlsButton,
        &corrSettings.secondGraphButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    corrSettings.filledDisplayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrFilledDisplayParameterId, corrSettings.filledDisplayButton);
    corrSettings.secondGraphAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrSecondGraphParameterId, corrSettings.secondGraphButton);
    corrSettings.clearOnPlayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrClearOnPlayParameterId, corrSettings.clearOnPlayButton);
    corrSettings.rangesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrRangesVisibleParameterId, corrSettings.rangesButton);
    corrSettings.cursorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrCursorReadoutParameterId, corrSettings.cursorButton);
    corrSettings.zoomControlsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrZoomControlsParameterId, corrSettings.zoomControlsButton);
    lvlsSettings.clearOnPlayButton.setClickingTogglesState(true);
    lvlsSettings.clearOnPlayButton.onClick = displaySettingChanged;
    lvlsSettings.clearOnPlayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsClearOnPlayParameterId, lvlsSettings.clearOnPlayButton);
    for (const auto& [button, parameterId] : std::array {
             std::pair { &lvlsSettings.peakRmsVisibleButton, PluginProcessor::lvlsPeakRmsVisibleParameterId },
             std::pair { &lvlsSettings.loudnessVisibleButton, PluginProcessor::lvlsLoudnessVisibleParameterId },
             std::pair { &lvlsSettings.historyVisibleButton, PluginProcessor::lvlsHistoryVisibleParameterId } })
    {
        button->setClickingTogglesState(true);
        button->onClick = [this, displaySettingChanged, button, parameterId]
        {
            const auto anyVisible = lvlsSettings.peakRmsVisibleButton.getToggleState()
                || lvlsSettings.loudnessVisibleButton.getToggleState()
                || lvlsSettings.historyVisibleButton.getToggleState();
            if (! anyVisible)
            {
                if (auto* parameter = processor.getParameters().getParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
           #if ANA_VARIANT_RT
            processor.clearLvlsProcessor();
           #endif
            displaySettingChanged();
        };
    }
    lvlsSettings.peakRmsVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsPeakRmsVisibleParameterId, lvlsSettings.peakRmsVisibleButton);
    lvlsSettings.loudnessVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsLoudnessVisibleParameterId,
        lvlsSettings.loudnessVisibleButton);
    lvlsSettings.historyVisibleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryVisibleParameterId,
        lvlsSettings.historyVisibleButton);
    for (const auto& [button, parameterId] : std::array {
             std::pair { &lvlsSettings.historyMomentaryButton, PluginProcessor::lvlsHistoryMomentaryVisibleParameterId },
             std::pair { &lvlsSettings.historyShortTermButton, PluginProcessor::lvlsHistoryShortTermVisibleParameterId },
             std::pair { &lvlsSettings.historyIntegratedButton, PluginProcessor::lvlsHistoryIntegratedVisibleParameterId } })
    {
        button->setClickingTogglesState(true);
        button->onClick = [this, displaySettingChanged, button, parameterId]
        {
            const auto anyVisible = lvlsSettings.historyMomentaryButton.getToggleState()
                || lvlsSettings.historyShortTermButton.getToggleState()
                || lvlsSettings.historyIntegratedButton.getToggleState();
            if (! anyVisible)
            {
                if (auto* parameter = processor.getParameters().getParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
            displaySettingChanged();
        };
    }
    lvlsSettings.historyMomentaryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryMomentaryVisibleParameterId,
        lvlsSettings.historyMomentaryButton);
    lvlsSettings.historyShortTermAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryShortTermVisibleParameterId,
        lvlsSettings.historyShortTermButton);
    lvlsSettings.historyIntegratedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryIntegratedVisibleParameterId,
        lvlsSettings.historyIntegratedButton);
    lvlsSettings.historyZoomButton.setClickingTogglesState(true);
    lvlsSettings.historyZoomButton.onClick = displaySettingChanged;
    lvlsSettings.historyZoomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryZoomParameterId,
        lvlsSettings.historyZoomButton);
    const auto requestChoice = [this] (ParameterControl& control)
    {
        if (onChoiceRequested)
            onChoiceRequested(control);
    };
    scopSettings.styleControl.onChoiceRequested = requestChoice;
    scopSettings.timeNoteControl.onChoiceRequested = requestChoice;
    scopSettings.timeBaseControl.onChoiceRequested = requestChoice;
    specSettings.fftSizeControl.onChoiceRequested = requestChoice;
    specSettings.mapTimeOverlapControl.onChoiceRequested = requestChoice;
    specSettings.frequencyScaleControl.onChoiceRequested = requestChoice;
    specSettings.firstGraphTypeControl.onChoiceRequested = requestChoice;
    specSettings.secondGraphTypeControl.onChoiceRequested = requestChoice;
    corrSettings.fftSizeControl.onChoiceRequested = requestChoice;
    corrSettings.frequencyScaleControl.onChoiceRequested = requestChoice;
    corrSettings.firstGraphTypeControl.onChoiceRequested = requestChoice;
    corrSettings.secondGraphTypeControl.onChoiceRequested = requestChoice;

    const auto requestReset = [this] (ParameterControl& control)
    {
        if (onResetRequested)
            onResetRequested(control);
    };
    for (auto* control : std::array<ParameterControl*, 30> {
             &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl,
             &specSettings.fftSizeControl, &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.averageTimeControl,
             &specSettings.smoothingControl, &specSettings.frequencyScaleControl,
             &specSettings.firstGraphTypeControl,
             &specSettings.secondGraphTypeControl, &specSettings.slopeControl,
             &specSettings.rangeLowControl, &specSettings.rangeHighControl,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl,
             &corrSettings.secondGraphTypeControl,
             &lvlsSettings.widthControl, &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl })
    {
        control->onResetRequested = requestReset;
        control->onTextEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
    }

    const auto focusControl = [this] (ParameterControl& control)
    {
        focusParameterControl(control);
    };
    scopSettings.opacityControl.onFocusRequested = focusControl;
    scopSettings.timeControl.onFocusRequested = focusControl;
    specSettings.averageTimeControl.onFocusRequested = focusControl;
    specSettings.smoothingControl.onFocusRequested = focusControl;
    specSettings.slopeControl.onFocusRequested = focusControl;
    specSettings.rangeLowControl.onFocusRequested = focusControl;
    specSettings.rangeHighControl.onFocusRequested = focusControl;
    specSettings.fftOverlapControl.onFocusRequested = focusControl;
    corrSettings.fftOverlapControl.onFocusRequested = focusControl;
    corrSettings.averageTimeControl.onFocusRequested = focusControl;
    corrSettings.smoothingControl.onFocusRequested = focusControl;
    lvlsSettings.widthControl.onFocusRequested = focusControl;
    lvlsSettings.peakRangeHighControl.onFocusRequested = focusControl;
    lvlsSettings.peakRangeLowControl.onFocusRequested = focusControl;
    lvlsSettings.rmsWindowControl.onFocusRequested = focusControl;
    lvlsSettings.peakHoldControl.onFocusRequested = focusControl;
    lvlsSettings.loudnessRangeHighControl.onFocusRequested = focusControl;
    lvlsSettings.loudnessRangeLowControl.onFocusRequested = focusControl;
    lvlsSettings.widthControl.onValueChanged = displaySettingChanged;
    lvlsSettings.peakRangeHighControl.onValueChanged = displaySettingChanged;
    lvlsSettings.peakRangeLowControl.onValueChanged = displaySettingChanged;
    lvlsSettings.rmsWindowControl.onValueChanged = displaySettingChanged;
    lvlsSettings.peakHoldControl.onValueChanged = displaySettingChanged;
    lvlsSettings.loudnessRangeHighControl.onValueChanged = displaySettingChanged;
    lvlsSettings.loudnessRangeLowControl.onValueChanged = displaySettingChanged;
    specSettings.smoothingControl.onValueChanged = displaySettingChanged;
    specSettings.frequencyScaleControl.onValueChanged = displaySettingChanged;
    specSettings.slopeControl.onValueChanged = displaySettingChanged;
    specSettings.rangeLowControl.onValueChanged = displaySettingChanged;
    specSettings.rangeHighControl.onValueChanged = displaySettingChanged;
    corrSettings.smoothingControl.onValueChanged = displaySettingChanged;
    corrSettings.frequencyScaleControl.onValueChanged = displaySettingChanged;
    specSettings.fftOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    corrSettings.fftOverlapControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 0.01;
    };
    scopSettings.timeBaseControl.onValueChanged = [this]
    {
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    scopSettings.timeNoteControl.onValueChanged = displaySettingChanged;
    scopSettings.timeControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue() * 1000.0;
    };

    focusedParameterControl.onValueChange = [this]
    {
        if (updatingFocusedParameterControl || focusedParameterTarget == nullptr
            || ! focusedParameterTarget->isInteractionEnabled())
            return;

        focusedParameterTarget->commitPendingEditor();
        auto& target = focusedParameterTarget->getSlider();
        const auto value = target.getNormalisableRange().convertFrom0to1(focusedParameterControl.getValue());
        target.setValue(value, juce::sendNotificationSync);
        syncFocusedParameterControl();
    };

    for (size_t index = 0; index < scopSettings.crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<ParameterControl>(
            processor.getParameters(),
            PluginProcessor::crossoverParameterIds[index],
            "CROSS-" + juce::String(static_cast<int>(index + 1)),
            [] (const double value) { return formatFrequency(value); });
        control->getSlider().valueFromTextFunction = [] (const juce::String& text)
        {
            return parseFrequency(text);
        };
        control->onValueChanged = [this, index] { constrainFrequency(index); };
        control->onFocusRequested = focusControl;
        control->onResetRequested = requestReset;
        control->onTextEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
        settingsContent.addAndMakeVisible(*control);
        scopSettings.crossoverControls[index] = std::move(control);
    }

    for (size_t index = 0; index < processor.getCrossoverCount(); ++index)
        constrainFrequency(index);

    refreshExternalState();
    clearFocusedParameterControl();
    settingsContent.addMouseListener(this, true);
    startTimerHz(15);
}

SettingsPanel::~SettingsPanel()
{
    settingsContent.removeMouseListener(this);
    settingsViewport.setViewedComponent(nullptr, false);
    settingsViewport.setLookAndFeel(nullptr);
}

void SettingsPanel::setAnalyzerContext(const ana::AnalyzerPage page,
                                       const juce::String& viewMode)
{
    const auto normalisedViewMode = viewMode.trim().toUpperCase();
    const auto pageChanged = analyzerPage != page;
    const auto viewModeChanged = analyzerViewMode != normalisedViewMode;
    if (! pageChanged && ! viewModeChanged)
        return;

    analyzerPage = page;
    analyzerViewMode = normalisedViewMode;
    updateGeneralHeading();

    clearFocusedParameterControl();
    refreshExternalState();
    resized();
}

void SettingsPanel::updateGeneralHeading()
{
    juce::String mainHeading;
    if (analyzerPage == ana::AnalyzerPage::spec)
        mainHeading = "SPEC";
    else if (analyzerPage == ana::AnalyzerPage::corr)
        mainHeading = "CORR";
    else if (analyzerPage == ana::AnalyzerPage::lvls)
        mainHeading = "LVLS";
    else
    {
        generalHeadingLabel.setText("CROSSOVER", juce::dontSendNotification);
        return;
    }

    if ((analyzerPage == ana::AnalyzerPage::spec || analyzerPage == ana::AnalyzerPage::corr)
        && analyzerViewMode.isNotEmpty())
        mainHeading += " " + analyzerViewMode;

    generalHeadingLabel.setText(mainHeading + " MAIN", juce::dontSendNotification);
}

void SettingsPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::black);
}

void SettingsPanel::resized()
{
    constexpr int headingHeight = ana::ui::controlHeight;
    constexpr int rowHeight = ana::ui::controlHeight;
    const auto& fixedGap = ana::ui::gap;
    const auto scopPage = analyzerPage == ana::AnalyzerPage::scop;
    const auto specPage = analyzerPage == ana::AnalyzerPage::spec;
    const auto specMapPage = specPage && analyzerViewMode == "MAP";
   #if ANA_VARIANT_RT
    const auto rtSpecMapPage = specMapPage;
   #else
    constexpr auto rtSpecMapPage = false;
   #endif
    const auto lvlsPage = analyzerPage == ana::AnalyzerPage::lvls;
   #if ANA_VARIANT_RT
    constexpr auto scopRowCount = 18;
   #else
    constexpr auto scopRowCount = 17;
   #endif
    const auto pageContentHeight = scopPage
        ? scopRowCount * rowHeight + (scopRowCount - 1) * fixedGap.pixels()
        : specPage ? (specMapPage
            ? (rtSpecMapPage
                ? 15 * rowHeight + 14 * fixedGap.pixels()
                : 14 * rowHeight + 13 * fixedGap.pixels())
            : 18 * rowHeight + 17 * fixedGap.pixels())
        : lvlsPage ? 16 * rowHeight + 15 * fixedGap.pixels() : 15 * rowHeight + 14 * fixedGap.pixels();
    const auto contentHeight = pageContentHeight;
    auto innerBounds = getLocalBounds().reduced(fixedGap.pixels());
    const auto potentiometerBounds = innerBounds.removeFromBottom(rowHeight);
    fixedGap.removeFromBottom(innerBounds);
    const auto viewportBounds = innerBounds;
    settingsViewport.setBounds(viewportBounds);
    const auto contentWidth = std::max(1, viewportBounds.getWidth());
    settingsContent.setSize(contentWidth, std::max(contentHeight, viewportBounds.getHeight()));
    auto potentiometerRow = potentiometerBounds;
    closeButton.setBounds(potentiometerRow.removeFromRight(ana::ui::iconControlSize));
    fixedGap.removeFromRight(potentiometerRow);
    focusedParameterControl.setBounds(potentiometerRow);
    auto area = settingsContent.getLocalBounds();
    const auto placeButton = [&] (ControlButton& button)
    {
        button.setBounds(area.removeFromTop(rowHeight));
    };


    if (! scopPage)
    {
        generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
    }

    if (lvlsPage)
    {
        lvlsSettings.rmsWindowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        lvlsSettings.peakHoldControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.clearOnPlayButton);
        fixedGap.removeFromTop(area);
        lvlsSettings.widthControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.centerSectionsButton);
        fixedGap.removeFromTop(area);
        lvlsSettings.peakRangeHighControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        lvlsSettings.peakRangeLowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        lvlsSettings.loudnessRangeHighControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        lvlsSettings.loudnessRangeLowControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.peakRmsVisibleButton);
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.loudnessVisibleButton);
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.historyVisibleButton);
        fixedGap.removeFromTop(area);
        auto historyButtons = area.removeFromTop(rowHeight);
        const auto historyButtonWidth = std::max(0,
            (historyButtons.getWidth() - fixedGap.pixels() * 2) / 3);
        lvlsSettings.historyMomentaryButton.setBounds(historyButtons.removeFromLeft(historyButtonWidth));
        fixedGap.removeFromLeft(historyButtons);
        lvlsSettings.historyShortTermButton.setBounds(historyButtons.removeFromLeft(historyButtonWidth));
        fixedGap.removeFromLeft(historyButtons);
        lvlsSettings.historyIntegratedButton.setBounds(historyButtons);
        fixedGap.removeFromTop(area);
        placeButton(lvlsSettings.historyZoomButton);
        return;
    }

    if (specPage)
    {
        specSettings.frequencyScaleControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        specSettings.fftSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        if (! specMapPage)
        {
            specSettings.fftOverlapControl.setBounds(area.removeFromTop(rowHeight));
            specSettings.mapTimeOverlapControl.setBounds({});
            fixedGap.removeFromTop(area);
        }
        else if (specMapPage)
        {
            specSettings.fftOverlapControl.setBounds({});
            specSettings.mapTimeOverlapControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.fftOverlapControl.setBounds({});
            specSettings.mapTimeOverlapControl.setBounds({});
        }

        if (! specMapPage)
        {
            specSettings.averageTimeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.smoothingControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.averageTimeControl.setBounds({});
            specSettings.smoothingControl.setBounds({});
        }

        if (specMapPage)
        {
            specSettings.slopeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.rangeHighControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.rangeLowControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            placeButton(specSettings.highQualityRenderingButton);
            fixedGap.removeFromTop(area);
            if (rtSpecMapPage)
            {
                placeButton(specSettings.mapLeftToRightButton);
                fixedGap.removeFromTop(area);
            }
            else
            {
                specSettings.mapLeftToRightButton.setBounds({});
            }
        }
        else
        {
            specSettings.rangeLowControl.setBounds({});
            specSettings.rangeHighControl.setBounds({});
            specSettings.highQualityRenderingButton.setBounds({});
            specSettings.mapLeftToRightButton.setBounds({});
            specSettings.slopeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);

        if (! specMapPage)
        {
            placeButton(specSettings.filledDisplayButton);
            fixedGap.removeFromTop(area);
            placeButton(specSettings.secondGraphButton);
            fixedGap.removeFromTop(area);
            specSettings.firstGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.secondGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.filledDisplayButton.setBounds({});
            specSettings.secondGraphButton.setBounds({});
            specSettings.firstGraphTypeControl.setBounds({});
            specSettings.secondGraphTypeControl.setBounds({});
        }

        placeButton(specSettings.clearOnPlayButton);
        fixedGap.removeFromTop(area);
        placeButton(specSettings.rangesButton);
        fixedGap.removeFromTop(area);
        placeButton(specSettings.cursorButton);
        fixedGap.removeFromTop(area);
        placeButton(specSettings.monitorControlsButton);
        fixedGap.removeFromTop(area);
        placeButton(specSettings.zoomControlsButton);

        if (! specMapPage)
        {
            fixedGap.removeFromTop(area);
            placeButton(specSettings.antiAliasButton);
        }
        else
        {
            specSettings.antiAliasButton.setBounds({});
        }
        return;
    }

    if (! scopPage)
    {
        corrSettings.frequencyScaleControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.fftSizeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.fftOverlapControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.averageTimeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.smoothingControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.filledDisplayButton);
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.secondGraphButton);
        fixedGap.removeFromTop(area);
        corrSettings.firstGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.secondGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.clearOnPlayButton);
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.rangesButton);
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.cursorButton);
        fixedGap.removeFromTop(area);
        placeButton(corrSettings.zoomControlsButton);
        return;
    }

    scopMainHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    const auto timeBounds = area.removeFromTop(rowHeight);
    scopSettings.timeControl.setBounds(timeBounds);
    scopSettings.timeNoteControl.setBounds(timeBounds);
    fixedGap.removeFromTop(area);
    scopSettings.timeBaseControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);

    generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    auto crossoverButtons = area.removeFromTop(rowHeight);
    const auto crossoverButtonWidth = std::max(0,
        (crossoverButtons.getWidth() - fixedGap.pixels()) / 2);
    scopSettings.addCrossoverButton.setBounds(crossoverButtons.removeFromLeft(crossoverButtonWidth));
    fixedGap.removeFromLeft(crossoverButtons);
    scopSettings.removeCrossoverButton.setBounds(crossoverButtons);
    fixedGap.removeFromTop(area);

    for (auto& control : scopSettings.crossoverControls)
    {
        control->setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
    }

    controlsVisibilityHeadingLabel.setBounds(area.removeFromTop(headingHeight));
    fixedGap.removeFromTop(area);
    placeButton(scopSettings.equalHeightButton);
    fixedGap.removeFromTop(area);
    scopSettings.styleControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
    scopSettings.opacityControl.setBounds(area.removeFromTop(rowHeight));
    fixedGap.removeFromTop(area);
   #if ANA_VARIANT_RT
    placeButton(scopSettings.leftToRightButton);
    fixedGap.removeFromTop(area);
   #else
    scopSettings.leftToRightButton.setBounds({});
   #endif
    placeButton(scopSettings.zoomControlsButton);
    fixedGap.removeFromTop(area);
    placeButton(scopSettings.monitorControlsButton);
    fixedGap.removeFromTop(area);
    placeButton(scopSettings.toolsButton);
}


void SettingsPanel::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != &settingsContent && event.originalComponent != this)
        return;

    dismissParameterEditors();
    clearFocusedParameterControl();

    draggedWindow = getTopLevelComponent();
    if (draggedWindow != nullptr && draggedWindow != this)
    {
        draggingWindow = true;
        windowDragStartMouseScreen = event.getScreenPosition();
        windowDragStartTopLeft = draggedWindow->getScreenBounds().getPosition();
    }
}

void SettingsPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (! draggingWindow || draggedWindow == nullptr)
        return;

    const auto delta = event.getScreenPosition() - windowDragStartMouseScreen;
    draggedWindow->setTopLeftPosition(windowDragStartTopLeft + delta);
}

void SettingsPanel::mouseUp(const juce::MouseEvent&)
{
    draggingWindow = false;
    draggedWindow = nullptr;
}

void SettingsPanel::timerCallback()
{
    refreshExternalState();
    syncFocusedParameterControl();
}

void SettingsPanel::changeCrossoverCount(const int delta)
{
    const auto currentCount = static_cast<int>(processor.getCrossoverCount());
    const auto newCount = juce::jlimit(0, static_cast<int>(scopSettings.crossoverControls.size()), currentCount + delta);

    if (newCount != currentCount)
    {
        processor.setCrossoverCount(static_cast<size_t>(newCount));

        for (int index = 0; index < newCount; ++index)
            constrainFrequency(static_cast<size_t>(index));
    }

    refreshExternalState();
}

void SettingsPanel::constrainFrequency(const size_t crossoverIndex)
{
    if (constrainingFrequency || crossoverIndex >= processor.getCrossoverCount())
        return;

    const juce::ScopedValueSetter<bool> guard(constrainingFrequency, true);
    const auto crossoverCount = processor.getCrossoverCount();
    auto& slider = scopSettings.crossoverControls[crossoverIndex]->getSlider();
    auto lowerBound = slider.getMinimum();
    auto upperBound = slider.getMaximum();

    if (crossoverIndex > 0)
        lowerBound = scopSettings.crossoverControls[crossoverIndex - 1]->getSlider().getValue() + minimumCrossoverGapHz;

    if (crossoverIndex + 1 < crossoverCount)
        upperBound = scopSettings.crossoverControls[crossoverIndex + 1]->getSlider().getValue() - minimumCrossoverGapHz;

    slider.setValue(juce::jlimit(lowerBound, std::max(lowerBound, upperBound), slider.getValue()),
                    juce::sendNotificationSync);
}

void SettingsPanel::refreshExternalState()
{
   #if ANA_VARIANT_RT
    constexpr auto rtControlsEnabled = true;
   #else
    constexpr auto rtControlsEnabled = false;
   #endif
    const auto scopPage = analyzerPage == ana::AnalyzerPage::scop;
    const auto specPage = analyzerPage == ana::AnalyzerPage::spec;
    const auto specMapPage = specPage && analyzerViewMode == "MAP";
    const auto corrPage = analyzerPage == ana::AnalyzerPage::corr;
    const auto lvlsPage = analyzerPage == ana::AnalyzerPage::lvls;

    // ARA exposes only controls used by its offline analysis workflow.
    scopSettings.timeControl.setInteractionEnabled(rtControlsEnabled, true);
    scopSettings.timeNoteControl.setInteractionEnabled(rtControlsEnabled, true);
    scopSettings.timeBaseControl.setInteractionEnabled(rtControlsEnabled, true);
    specSettings.averageTimeControl.setInteractionEnabled(rtControlsEnabled, true);
    specSettings.firstGraphTypeControl.setInteractionEnabled(rtControlsEnabled, true);
    specSettings.secondGraphTypeControl.setInteractionEnabled(rtControlsEnabled, true);
    corrSettings.averageTimeControl.setInteractionEnabled(rtControlsEnabled, true);
    corrSettings.firstGraphTypeControl.setInteractionEnabled(rtControlsEnabled, true);
    corrSettings.secondGraphTypeControl.setInteractionEnabled(rtControlsEnabled, true);
    lvlsSettings.rmsWindowControl.setInteractionEnabled(rtControlsEnabled, true);
    lvlsSettings.peakHoldControl.setInteractionEnabled(rtControlsEnabled, true);
    specSettings.clearOnPlayButton.setEnabled(rtControlsEnabled);
    specSettings.secondGraphButton.setEnabled(rtControlsEnabled);
    corrSettings.clearOnPlayButton.setEnabled(rtControlsEnabled);
    corrSettings.secondGraphButton.setEnabled(rtControlsEnabled);
    lvlsSettings.clearOnPlayButton.setEnabled(rtControlsEnabled);
    controlsVisibilityHeadingLabel.setVisible(scopPage || specPage || corrPage || lvlsPage);
    for (auto* component : std::array<juce::Component*, 12> {
             &scopSettings.addCrossoverButton, &scopSettings.removeCrossoverButton, &scopSettings.equalHeightButton,
             &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.zoomControlsButton,
             &scopSettings.monitorControlsButton, &scopSettings.toolsButton, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl,
             &scopSettings.leftToRightButton })
        component->setVisible(scopPage);
   #if ! ANA_VARIANT_RT
    scopSettings.leftToRightButton.setVisible(false);
   #endif
    for (auto& control : scopSettings.crossoverControls)
        control->setVisible(scopPage);
    scopMainHeadingLabel.setVisible(scopPage);

    specSettings.fftSizeControl.setVisible(specPage);
    specSettings.fftOverlapControl.setVisible(specPage && ! specMapPage);
    specSettings.mapTimeOverlapControl.setVisible(specMapPage);
    specSettings.slopeControl.setVisible(specPage);
    specSettings.rangeLowControl.setVisible(specMapPage);
    specSettings.rangeHighControl.setVisible(specMapPage);
    specSettings.highQualityRenderingButton.setVisible(specMapPage);
    specSettings.mapLeftToRightButton.setVisible(specMapPage && rtControlsEnabled);
    specSettings.clearOnPlayButton.setVisible(specPage);
    specSettings.rangesButton.setVisible(specPage);
    specSettings.cursorButton.setVisible(specPage);
    specSettings.monitorControlsButton.setVisible(specPage);
    specSettings.zoomControlsButton.setVisible(specPage);

    // Hide FREQ-only line controls in MAP; they do not participate in raster rendering.
    const auto showFreqOnlySpecSettings = specPage && ! specMapPage;
    specSettings.averageTimeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.smoothingControl.setVisible(showFreqOnlySpecSettings);
    specSettings.filledDisplayButton.setVisible(showFreqOnlySpecSettings);
    specSettings.secondGraphButton.setVisible(showFreqOnlySpecSettings);
    specSettings.firstGraphTypeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.secondGraphTypeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.antiAliasButton.setVisible(showFreqOnlySpecSettings);

    for (auto* component : std::array<juce::Component*, 12> {
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.firstGraphTypeControl,
             &corrSettings.secondGraphTypeControl,
             &corrSettings.filledDisplayButton, &corrSettings.secondGraphButton,
             &corrSettings.clearOnPlayButton, &corrSettings.rangesButton,
             &corrSettings.cursorButton, &corrSettings.zoomControlsButton })
        component->setVisible(corrPage);

    for (auto* component : std::array<juce::Component*, 15> {
             &lvlsSettings.widthControl, &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.peakRmsVisibleButton,
             &lvlsSettings.loudnessVisibleButton, &lvlsSettings.historyVisibleButton,
             &lvlsSettings.historyMomentaryButton, &lvlsSettings.historyShortTermButton,
             &lvlsSettings.historyIntegratedButton, &lvlsSettings.historyZoomButton })
        component->setVisible(lvlsPage);
    lvlsSettings.centerSectionsButton.setVisible(lvlsPage);

    if (focusedParameterTarget != nullptr
        && (! focusedParameterTarget->isInteractionEnabled() || ! focusedParameterTarget->isVisible()))
        clearFocusedParameterControl();

    if (! scopPage)
        return;

    const auto crossoverCount = processor.getCrossoverCount();
    scopSettings.addCrossoverButton.setEnabled(crossoverCount < scopSettings.crossoverControls.size());
    scopSettings.removeCrossoverButton.setEnabled(crossoverCount > 0);

    for (size_t index = 0; index < scopSettings.crossoverControls.size(); ++index)
        scopSettings.crossoverControls[index]->setInteractionEnabled(index < crossoverCount);

    const auto noteTime = processor.isScopTimeNoteBased();
    scopSettings.timeControl.setVisible(! noteTime);
    scopSettings.timeNoteControl.setVisible(noteTime);

    if (noteTime && focusedParameterTarget == &scopSettings.timeControl)
        clearFocusedParameterControl();

}

void SettingsPanel::focusParameterControl(ParameterControl& control)
{
    if (! control.isInteractionEnabled() || ! control.supportsFocusedPotentiometer())
        return;

    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = &control;
    focusedParameterTarget->setSelected(true);

    syncFocusedParameterControl();
    focusedParameterControl.setEnabled(true);
}

void SettingsPanel::clearFocusedParameterControl()
{
    if (focusedParameterTarget != nullptr)
        focusedParameterTarget->setSelected(false);

    focusedParameterTarget = nullptr;
    focusedParameterControl.setEnabled(false);
}

void SettingsPanel::dismissParameterEditors()
{
    scopSettings.styleControl.commitPendingEditor();
    scopSettings.opacityControl.commitPendingEditor();
    scopSettings.timeControl.commitPendingEditor();
    scopSettings.timeNoteControl.commitPendingEditor();
    scopSettings.timeBaseControl.commitPendingEditor();
    specSettings.fftSizeControl.commitPendingEditor();
    specSettings.fftOverlapControl.commitPendingEditor();
    specSettings.mapTimeOverlapControl.commitPendingEditor();
    specSettings.averageTimeControl.commitPendingEditor();
    specSettings.smoothingControl.commitPendingEditor();
    specSettings.firstGraphTypeControl.commitPendingEditor();
    specSettings.secondGraphTypeControl.commitPendingEditor();
    specSettings.slopeControl.commitPendingEditor();
    specSettings.rangeLowControl.commitPendingEditor();
    specSettings.rangeHighControl.commitPendingEditor();
    corrSettings.fftSizeControl.commitPendingEditor();
    corrSettings.fftOverlapControl.commitPendingEditor();
    corrSettings.averageTimeControl.commitPendingEditor();
    corrSettings.smoothingControl.commitPendingEditor();
    lvlsSettings.widthControl.commitPendingEditor();
    lvlsSettings.peakRangeHighControl.commitPendingEditor();
    lvlsSettings.peakRangeLowControl.commitPendingEditor();
    lvlsSettings.rmsWindowControl.commitPendingEditor();
    lvlsSettings.peakHoldControl.commitPendingEditor();
    lvlsSettings.loudnessRangeHighControl.commitPendingEditor();
    lvlsSettings.loudnessRangeLowControl.commitPendingEditor();

    for (auto& control : scopSettings.crossoverControls)
        control->commitPendingEditor();
}

void SettingsPanel::syncFocusedParameterControl()
{
    if (focusedParameterTarget == nullptr
        || ! focusedParameterTarget->isInteractionEnabled()
        || ! focusedParameterTarget->supportsFocusedPotentiometer())
        return;

    const auto& target = focusedParameterTarget->getSlider();
    const auto normalisedValue = target.getNormalisableRange().convertTo0to1(target.getValue());

    if (std::abs(focusedParameterControl.getValue() - normalisedValue) > 1.0e-6)
    {
        const juce::ScopedValueSetter<bool> guard(updatingFocusedParameterControl, true);
        focusedParameterControl.setValue(normalisedValue, juce::dontSendNotification);
    }
}
