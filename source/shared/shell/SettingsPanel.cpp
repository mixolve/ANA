#include "SettingsPanel.h"
#include "Processor.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <tuple>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;

juce::String formatFrequency(const double frequency)
{
    return juce::String::formatted("%08.2f", std::max(0.0, frequency));
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
    configureHeading(lvlsPeakHeadingLabel, "LVLS - PEAK/RMS");
    configureHeading(lvlsLoudnessHeadingLabel, "LVLS - LOUDNESS");
    configureHeading(lvlsHistoryHeadingLabel, "LVLS - HISTORY");

    for (auto* button : std::array<ControlButton*, 6> {
             &specSettings.cursorButton, &specSettings.cursorNotesButton,
             &specSettings.cursorVerticalButton, &corrSettings.cursorButton,
             &corrSettings.cursorNotesButton, &corrSettings.cursorVerticalButton })
        button->setCondenseTextToFit(true);
    for (auto* button : std::array<ControlButton*, 6> {
             &specSettings.clearOnPlayButton, &specSettings.resetClickButton,
             &corrSettings.clearOnPlayButton, &corrSettings.resetClickButton,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.resetClickButton })
        button->setCondenseTextToFit(true);

    for (auto* component : std::array<juce::Component*, 89> {
             &scopSettings.addCrossoverButton, &scopSettings.removeCrossoverButton, &scopSettings.equalHeightButton,
             &scopSettings.styleControl, &scopSettings.opacityControl,
             &scopSettings.horizontalZoomButton, &scopSettings.verticalZoomButton,
             &scopSettings.horizontalReadoutsButton, &scopSettings.verticalReadoutsButton,
             &scopSettings.monitorControlsButton, &scopSettings.toolsButton, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl, &scopSettings.leftToRightButton,
             &specSettings.fftSizeControl,
             &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.mapTimeControl, &specSettings.mapTimeNoteControl,
             &specSettings.mapTimeBaseControl,
             &specSettings.averageTimeControl, &specSettings.smoothingControl,
             &specSettings.frequencyScaleControl, &specSettings.mapColourMapControl,
             &specSettings.filledDisplayButton, &specSettings.secondGraphButton,
             &specSettings.firstGraphTypeControl, &specSettings.firstGraphColourControl,
             &specSettings.secondGraphTypeControl, &specSettings.secondGraphColourControl,
             &specSettings.graphOpacityControl,
             &specSettings.antiAliasButton, &specSettings.highQualityRenderingButton, &specSettings.mapLeftToRightButton,
             &specSettings.slopeControl,
             &specSettings.rangeLowControl, &specSettings.rangeHighControl,
             &specSettings.clearOnPlayButton, &specSettings.resetClickButton,
             &specSettings.horizontalReadoutsButton,
             &specSettings.verticalReadoutsButton, &specSettings.cursorButton,
             &specSettings.cursorNotesButton, &specSettings.cursorVerticalButton,
             &specSettings.monitorControlsButton, &specSettings.horizontalZoomButton,
             &specSettings.verticalZoomButton,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl, &corrSettings.firstGraphColourControl,
             &corrSettings.secondGraphTypeControl, &corrSettings.secondGraphColourControl,
             &corrSettings.graphOpacityControl,
             &corrSettings.filledDisplayButton, &corrSettings.secondGraphButton,
             &corrSettings.clearOnPlayButton, &corrSettings.resetClickButton,
             &corrSettings.horizontalReadoutsButton, &corrSettings.verticalReadoutsButton,
             &corrSettings.cursorButton, &corrSettings.cursorNotesButton, &corrSettings.cursorVerticalButton, &corrSettings.horizontalZoomButton,
             &corrSettings.verticalZoomButton, &lvlsSettings.widthControl,
             &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.resetClickButton,
             &lvlsSettings.centerSectionsButton,
             &lvlsSettings.peakRmsVisibleButton, &lvlsSettings.loudnessVisibleButton,
             &lvlsSettings.historyVisibleButton, &lvlsSettings.historyMomentaryButton,
             &lvlsSettings.historyShortTermButton, &lvlsSettings.historyIntegratedButton,
             &lvlsSettings.historyHorizontalZoomButton, &lvlsSettings.historyVerticalZoomButton,
             &lvlsSettings.historyHorizontalReadoutsButton, &lvlsSettings.historyVerticalReadoutsButton })
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
    for (auto* button : std::array<ControlButton*, 4> {
             &scopSettings.verticalZoomButton, &scopSettings.verticalReadoutsButton,
             &scopSettings.monitorControlsButton,
             &scopSettings.toolsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    scopSettings.verticalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopVerticalZoomControlsParameterId,
        scopSettings.verticalZoomButton);
    scopSettings.verticalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopVerticalReadoutsParameterId,
        scopSettings.verticalReadoutsButton);
    scopSettings.horizontalZoomButton.setClickingTogglesState(true);
    scopSettings.horizontalZoomButton.onClick = displaySettingChanged;
    scopSettings.horizontalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopHorizontalZoomControlsParameterId,
        scopSettings.horizontalZoomButton);
    scopSettings.horizontalReadoutsButton.setClickingTogglesState(true);
    scopSettings.horizontalReadoutsButton.onClick = displaySettingChanged;
    scopSettings.horizontalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopHorizontalReadoutsParameterId,
        scopSettings.horizontalReadoutsButton);
    scopSettings.monitorControlsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopMonitorControlsParameterId, scopSettings.monitorControlsButton);
    scopSettings.toolsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopToolsParameterId, scopSettings.toolsButton);
    scopSettings.leftToRightButton.setClickingTogglesState(true);
    scopSettings.leftToRightButton.onClick = displaySettingChanged;
    scopSettings.leftToRightAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::scopLeftToRightParameterId,
        scopSettings.leftToRightButton);
    scopSettings.leftToRightButton.setTooltip(
        "Draw REALTIME SCOP from left to right; clear and restart at the left edge after reaching the right edge");
    for (auto* button : std::array<ControlButton*, 15> {
             &specSettings.filledDisplayButton, &specSettings.secondGraphButton,
             &specSettings.antiAliasButton, &specSettings.highQualityRenderingButton,
             &specSettings.mapLeftToRightButton, &specSettings.clearOnPlayButton,
             &specSettings.resetClickButton,
             &specSettings.horizontalReadoutsButton, &specSettings.verticalReadoutsButton,
             &specSettings.cursorButton, &specSettings.cursorNotesButton,
             &specSettings.cursorVerticalButton,
             &specSettings.monitorControlsButton,
             &specSettings.horizontalZoomButton, &specSettings.verticalZoomButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    specSettings.filledDisplayAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specFilledDisplayParameterId, specSettings.filledDisplayButton);
    specSettings.secondGraphAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specSecondGraphParameterId, specSettings.secondGraphButton);
    specSettings.antiAliasAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specAntiAliasParameterId, specSettings.antiAliasButton);
    specSettings.highQualityRenderingAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specHighQualityRenderingParameterId, specSettings.highQualityRenderingButton);
    specSettings.highQualityRenderingButton.setTooltip(
        "Accurate max-bilinear interpolation of a spectrogram (recommended)");
    specSettings.mapLeftToRightAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specMapLeftToRightParameterId, specSettings.mapLeftToRightButton);
    specSettings.mapLeftToRightButton.setTooltip(
        "Draw REALTIME SPEC MAP from left to right; clear and restart at the left edge after reaching the right edge");
    specSettings.horizontalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specHorizontalReadoutsParameterId, specSettings.horizontalReadoutsButton);
    specSettings.verticalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specVerticalReadoutsParameterId, specSettings.verticalReadoutsButton);
    specSettings.clearOnPlayAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specClearOnPlayParameterId, specSettings.clearOnPlayButton);
    specSettings.resetClickAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specResetClickParameterId, specSettings.resetClickButton);
    specSettings.cursorAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specCursorReadoutParameterId, specSettings.cursorButton);
    specSettings.cursorNotesAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specCursorNotesParameterId, specSettings.cursorNotesButton);
    specSettings.cursorVerticalAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specCursorVerticalReadoutParameterId,
        specSettings.cursorVerticalButton);
    specSettings.monitorControlsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specMonitorControlsParameterId, specSettings.monitorControlsButton);
    specSettings.horizontalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specHorizontalZoomParameterId, specSettings.horizontalZoomButton);
    specSettings.verticalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::specVerticalZoomParameterId, specSettings.verticalZoomButton);
    for (auto* button : std::array<ControlButton*, 11> {
        &corrSettings.filledDisplayButton, &corrSettings.clearOnPlayButton,
        &corrSettings.resetClickButton,
        &corrSettings.horizontalReadoutsButton, &corrSettings.verticalReadoutsButton,
        &corrSettings.cursorButton, &corrSettings.cursorNotesButton, &corrSettings.cursorVerticalButton,
        &corrSettings.horizontalZoomButton, &corrSettings.verticalZoomButton,
        &corrSettings.secondGraphButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    corrSettings.filledDisplayAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrFilledDisplayParameterId, corrSettings.filledDisplayButton);
    corrSettings.secondGraphAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrSecondGraphParameterId, corrSettings.secondGraphButton);
    corrSettings.clearOnPlayAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrClearOnPlayParameterId, corrSettings.clearOnPlayButton);
    corrSettings.resetClickAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrResetClickParameterId, corrSettings.resetClickButton);
    corrSettings.horizontalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrHorizontalReadoutsParameterId, corrSettings.horizontalReadoutsButton);
    corrSettings.verticalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrVerticalReadoutsParameterId, corrSettings.verticalReadoutsButton);
    corrSettings.cursorAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrCursorReadoutParameterId, corrSettings.cursorButton);
    corrSettings.cursorNotesAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrCursorNotesParameterId, corrSettings.cursorNotesButton);
    corrSettings.cursorVerticalAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrCursorVerticalReadoutParameterId,
        corrSettings.cursorVerticalButton);
    corrSettings.horizontalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrHorizontalZoomParameterId, corrSettings.horizontalZoomButton);
    corrSettings.verticalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::corrVerticalZoomParameterId, corrSettings.verticalZoomButton);
    for (auto* component : std::array<juce::Component*, 7> {
             &lvlsSettings.loudnessWidthControl, &lvlsSettings.historyRangeHighControl,
             &lvlsSettings.historyRangeLowControl, &lvlsSettings.loudnessClearOnPlayButton,
             &lvlsSettings.loudnessResetClickButton, &lvlsSettings.historyClearOnPlayButton,
             &lvlsSettings.historyResetClickButton })
        settingsContent.addAndMakeVisible(*component);
    for (const auto& [button, id, attachment] : std::array {
             std::tuple { &lvlsSettings.loudnessClearOnPlayButton, PluginProcessor::lvlsLoudnessClearOnPlayParameterId,
                          &lvlsSettings.loudnessClearOnPlayAttachment },
             std::tuple { &lvlsSettings.loudnessResetClickButton, PluginProcessor::lvlsLoudnessResetClickParameterId,
                          &lvlsSettings.loudnessResetClickAttachment },
             std::tuple { &lvlsSettings.historyClearOnPlayButton, PluginProcessor::lvlsHistoryClearOnPlayParameterId,
                          &lvlsSettings.historyClearOnPlayAttachment },
             std::tuple { &lvlsSettings.historyResetClickButton, PluginProcessor::lvlsHistoryResetClickParameterId,
                          &lvlsSettings.historyResetClickAttachment } })
    {
        button->setClickingTogglesState(true);
        button->setCondenseTextToFit(true);
        button->onClick = displaySettingChanged;
        *attachment = std::make_unique<SubmoduleButtonAttachment>(processor.getParameters(), id, *button);
    }
    lvlsSettings.clearOnPlayButton.setClickingTogglesState(true);
    lvlsSettings.clearOnPlayButton.onClick = displaySettingChanged;
    lvlsSettings.clearOnPlayAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsClearOnPlayParameterId, lvlsSettings.clearOnPlayButton);
    lvlsSettings.resetClickButton.setClickingTogglesState(true);
    lvlsSettings.resetClickButton.onClick = displaySettingChanged;
    lvlsSettings.resetClickAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsResetClickParameterId, lvlsSettings.resetClickButton);
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
                if (auto* parameter = processor.getActiveParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
            processor.clearLvlsProcessor(parameterId == PluginProcessor::lvlsPeakRmsVisibleParameterId ? 0
                                         : parameterId == PluginProcessor::lvlsLoudnessVisibleParameterId ? 1 : 2);
            displaySettingChanged();
        };
    }
    lvlsSettings.peakRmsVisibleAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsPeakRmsVisibleParameterId, lvlsSettings.peakRmsVisibleButton);
    lvlsSettings.loudnessVisibleAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsLoudnessVisibleParameterId,
        lvlsSettings.loudnessVisibleButton);
    lvlsSettings.historyVisibleAttachment = std::make_unique<SubmoduleButtonAttachment>(
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
                if (auto* parameter = processor.getActiveParameter(parameterId))
                    parameter->setValueNotifyingHost(1.0f);
                button->setToggleState(true, juce::dontSendNotification);
            }
            displaySettingChanged();
        };
    }
    lvlsSettings.historyMomentaryAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryMomentaryVisibleParameterId,
        lvlsSettings.historyMomentaryButton);
    lvlsSettings.historyShortTermAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryShortTermVisibleParameterId,
        lvlsSettings.historyShortTermButton);
    lvlsSettings.historyIntegratedAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryIntegratedVisibleParameterId,
        lvlsSettings.historyIntegratedButton);
    for (auto* button : std::array<ControlButton*, 4> {
             &lvlsSettings.historyHorizontalZoomButton, &lvlsSettings.historyVerticalZoomButton,
             &lvlsSettings.historyHorizontalReadoutsButton, &lvlsSettings.historyVerticalReadoutsButton })
    {
        button->setClickingTogglesState(true);
        button->onClick = displaySettingChanged;
    }
    lvlsSettings.historyHorizontalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryHorizontalZoomParameterId,
        lvlsSettings.historyHorizontalZoomButton);
    lvlsSettings.historyVerticalZoomAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryVerticalZoomParameterId,
        lvlsSettings.historyVerticalZoomButton);
    lvlsSettings.historyHorizontalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryHorizontalReadoutsParameterId,
        lvlsSettings.historyHorizontalReadoutsButton);
    lvlsSettings.historyVerticalReadoutsAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsHistoryVerticalReadoutsParameterId,
        lvlsSettings.historyVerticalReadoutsButton);
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
    specSettings.mapTimeNoteControl.onChoiceRequested = requestChoice;
    specSettings.mapTimeBaseControl.onChoiceRequested = requestChoice;
    specSettings.frequencyScaleControl.onChoiceRequested = requestChoice;
    specSettings.mapColourMapControl.onChoiceRequested = requestChoice;
    specSettings.firstGraphTypeControl.onChoiceRequested = requestChoice;
    specSettings.firstGraphColourControl.onChoiceRequested = requestChoice;
    specSettings.secondGraphTypeControl.onChoiceRequested = requestChoice;
    specSettings.secondGraphColourControl.onChoiceRequested = requestChoice;
    corrSettings.fftSizeControl.onChoiceRequested = requestChoice;
    corrSettings.frequencyScaleControl.onChoiceRequested = requestChoice;
    corrSettings.firstGraphTypeControl.onChoiceRequested = requestChoice;
    corrSettings.firstGraphColourControl.onChoiceRequested = requestChoice;
    corrSettings.secondGraphTypeControl.onChoiceRequested = requestChoice;
    corrSettings.secondGraphColourControl.onChoiceRequested = requestChoice;

    const auto requestReset = [this] (ParameterControl& control)
    {
        if (onResetRequested)
            onResetRequested(control);
    };
    for (auto* control : std::array<ParameterControl*, 43> {
             &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl,
             &specSettings.fftSizeControl, &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.mapTimeControl, &specSettings.mapTimeNoteControl,
             &specSettings.mapTimeBaseControl,
             &specSettings.averageTimeControl,
             &specSettings.smoothingControl, &specSettings.frequencyScaleControl,
             &specSettings.mapColourMapControl,
             &specSettings.firstGraphTypeControl, &specSettings.firstGraphColourControl,
             &specSettings.secondGraphTypeControl, &specSettings.secondGraphColourControl,
             &specSettings.graphOpacityControl, &specSettings.slopeControl,
             &specSettings.rangeLowControl, &specSettings.rangeHighControl,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl, &corrSettings.firstGraphColourControl,
             &corrSettings.secondGraphTypeControl, &corrSettings.secondGraphColourControl,
             &corrSettings.graphOpacityControl,
             &lvlsSettings.widthControl, &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.loudnessWidthControl, &lvlsSettings.historyRangeHighControl,
             &lvlsSettings.historyRangeLowControl })
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
    specSettings.mapTimeControl.onFocusRequested = focusControl;
    specSettings.averageTimeControl.onFocusRequested = focusControl;
    specSettings.smoothingControl.onFocusRequested = focusControl;
    specSettings.graphOpacityControl.onFocusRequested = focusControl;
    specSettings.slopeControl.onFocusRequested = focusControl;
    specSettings.rangeLowControl.onFocusRequested = focusControl;
    specSettings.rangeHighControl.onFocusRequested = focusControl;
    specSettings.fftOverlapControl.onFocusRequested = focusControl;
    corrSettings.fftOverlapControl.onFocusRequested = focusControl;
    corrSettings.averageTimeControl.onFocusRequested = focusControl;
    corrSettings.smoothingControl.onFocusRequested = focusControl;
    corrSettings.graphOpacityControl.onFocusRequested = focusControl;
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
    for (auto* control : { &lvlsSettings.loudnessWidthControl,
                           &lvlsSettings.historyRangeHighControl,
                           &lvlsSettings.historyRangeLowControl })
    {
        control->onFocusRequested = focusControl;
        control->onValueChanged = displaySettingChanged;
    }
    specSettings.smoothingControl.onValueChanged = displaySettingChanged;
    specSettings.frequencyScaleControl.onValueChanged = displaySettingChanged;
    specSettings.mapColourMapControl.onValueChanged = displaySettingChanged;
    specSettings.firstGraphColourControl.onValueChanged = displaySettingChanged;
    specSettings.firstGraphTypeControl.onValueChanged = displaySettingChanged;
    specSettings.secondGraphColourControl.onValueChanged = displaySettingChanged;
    specSettings.graphOpacityControl.onValueChanged = displaySettingChanged;
    specSettings.slopeControl.onValueChanged = displaySettingChanged;
    specSettings.rangeLowControl.onValueChanged = displaySettingChanged;
    specSettings.rangeHighControl.onValueChanged = displaySettingChanged;
    corrSettings.smoothingControl.onValueChanged = displaySettingChanged;
    corrSettings.frequencyScaleControl.onValueChanged = displaySettingChanged;
    corrSettings.firstGraphColourControl.onValueChanged = displaySettingChanged;
    corrSettings.secondGraphColourControl.onValueChanged = displaySettingChanged;
    corrSettings.graphOpacityControl.onValueChanged = displaySettingChanged;
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
        return text.getDoubleValue();
    };
    specSettings.mapTimeBaseControl.onValueChanged = [this]
    {
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    specSettings.mapTimeNoteControl.onValueChanged = displaySettingChanged;
    specSettings.mapTimeControl.onValueChanged = displaySettingChanged;
    specSettings.mapTimeControl.getSlider().valueFromTextFunction = [] (const juce::String& text)
    {
        return text.getDoubleValue();
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

bool SettingsPanel::setAnalyzerContext(const ana::AnalyzerPage page,
                                       const juce::String& viewMode)
{
    const auto normalisedViewMode = viewMode.trim().toUpperCase();
    const auto pageChanged = analyzerPage != page;
    const auto viewModeChanged = analyzerViewMode != normalisedViewMode;
    if (! pageChanged && ! viewModeChanged)
        return false;

    analyzerPage = page;
    analyzerViewMode = normalisedViewMode;
    updateGeneralHeading();

    clearFocusedParameterControl();
    refreshExternalState();
    resized();
    return true;
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
    graphics.fillAll(ana::ui::background);
}

void SettingsPanel::resized()
{
    constexpr int headingHeight = ana::ui::controlHeight;
    constexpr int rowHeight = ana::ui::controlHeight;
    const auto& fixedGap = ana::ui::gap;
    const auto scopPage = analyzerPage == ana::AnalyzerPage::scop;
    const auto specPage = analyzerPage == ana::AnalyzerPage::spec;
    const auto specMapPage = specPage && analyzerViewMode == "MAP";
    const auto specHorizontalControls = ! specMapPage || processor.isOfflineMode();
    const auto realtimeSpecMapPage = specMapPage && ! processor.isOfflineMode();
    const auto lvlsPage = analyzerPage == ana::AnalyzerPage::lvls;
    const auto scopRowCount = processor.isOfflineMode() ? 18 : 19;
    const auto pageContentHeight = scopPage
        ? scopRowCount * rowHeight + (scopRowCount - 1) * fixedGap.pixels()
        : specPage ? (specMapPage
            ? (realtimeSpecMapPage
                ? 18 * rowHeight + 17 * fixedGap.pixels()
                : 17 * rowHeight + 16 * fixedGap.pixels())
            : 21 * rowHeight + 20 * fixedGap.pixels())
        : lvlsPage ? 24 * rowHeight + 23 * fixedGap.pixels() : 18 * rowHeight + 17 * fixedGap.pixels();
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
    const auto placeButtonPair = [&] (ControlButton& left, ControlButton& right)
    {
        auto row = area.removeFromTop(rowHeight);
        const auto leftWidth = std::max(0, (row.getWidth() - fixedGap.pixels()) / 2);
        left.setBounds(row.removeFromLeft(leftWidth));
        fixedGap.removeFromLeft(row);
        right.setBounds(row);
    };
    const auto placeButtonTriple = [&] (ControlButton& first, ControlButton& second,
                                       ControlButton& third)
    {
        auto row = area.removeFromTop(rowHeight);
        const auto firstWidth = std::max(0, (row.getWidth() - fixedGap.pixels() * 2) / 3);
        first.setBounds(row.removeFromLeft(firstWidth));
        fixedGap.removeFromLeft(row);
        second.setBounds(row.removeFromLeft(firstWidth));
        fixedGap.removeFromLeft(row);
        third.setBounds(row);
    };


    if (! scopPage)
    {
        generalHeadingLabel.setBounds(area.removeFromTop(headingHeight));
        fixedGap.removeFromTop(area);
    }

    if (lvlsPage)
    {
        const auto placeControl = [&] (juce::Component& component)
        {
            component.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        };
        const auto placeReset = [&] (ControlButton& clear, ControlButton& reset)
        {
           if (! processor.isOfflineMode())
           {
            placeButtonPair(clear, reset);
           }
           else
           {
            placeButton(clear);
            reset.setBounds({});
           }
            fixedGap.removeFromTop(area);
        };
        placeControl(lvlsSettings.centerSectionsButton);
        placeControl(lvlsPeakHeadingLabel);
        placeControl(lvlsSettings.peakRmsVisibleButton);
        placeControl(lvlsSettings.rmsWindowControl);
        placeControl(lvlsSettings.peakHoldControl);
        placeControl(lvlsSettings.widthControl);
        placeControl(lvlsSettings.peakRangeHighControl);
        placeControl(lvlsSettings.peakRangeLowControl);
        placeReset(lvlsSettings.clearOnPlayButton, lvlsSettings.resetClickButton);
        placeControl(lvlsLoudnessHeadingLabel);
        placeControl(lvlsSettings.loudnessVisibleButton);
        placeControl(lvlsSettings.loudnessWidthControl);
        placeControl(lvlsSettings.loudnessRangeHighControl);
        placeControl(lvlsSettings.loudnessRangeLowControl);
        placeReset(lvlsSettings.loudnessClearOnPlayButton, lvlsSettings.loudnessResetClickButton);
        placeControl(lvlsHistoryHeadingLabel);
        placeControl(lvlsSettings.historyVisibleButton);
        placeControl(lvlsSettings.historyRangeHighControl);
        placeControl(lvlsSettings.historyRangeLowControl);
        placeReset(lvlsSettings.historyClearOnPlayButton, lvlsSettings.historyResetClickButton);
        placeButtonTriple(lvlsSettings.historyMomentaryButton,
                          lvlsSettings.historyShortTermButton, lvlsSettings.historyIntegratedButton);
        fixedGap.removeFromTop(area);
        placeButtonPair(lvlsSettings.historyHorizontalZoomButton, lvlsSettings.historyVerticalZoomButton);
        fixedGap.removeFromTop(area);
        placeButtonPair(lvlsSettings.historyHorizontalReadoutsButton, lvlsSettings.historyVerticalReadoutsButton);
        return;
    }

    if (specPage)
    {
        if (specMapPage)
        {
            const auto timeBounds = area.removeFromTop(rowHeight);
            specSettings.mapTimeControl.setBounds(timeBounds);
            specSettings.mapTimeNoteControl.setBounds(timeBounds);
            fixedGap.removeFromTop(area);
            specSettings.mapTimeBaseControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.mapTimeControl.setBounds({});
            specSettings.mapTimeNoteControl.setBounds({});
            specSettings.mapTimeBaseControl.setBounds({});
        }

        specSettings.frequencyScaleControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        if (specMapPage)
        {
            specSettings.mapColourMapControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.mapColourMapControl.setBounds({});
        }
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
            if (realtimeSpecMapPage)
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
            specSettings.firstGraphColourControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.secondGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.secondGraphColourControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
            specSettings.graphOpacityControl.setBounds(area.removeFromTop(rowHeight));
            fixedGap.removeFromTop(area);
        }
        else
        {
            specSettings.filledDisplayButton.setBounds({});
            specSettings.secondGraphButton.setBounds({});
            specSettings.firstGraphTypeControl.setBounds({});
            specSettings.firstGraphColourControl.setBounds({});
            specSettings.secondGraphTypeControl.setBounds({});
            specSettings.secondGraphColourControl.setBounds({});
            specSettings.graphOpacityControl.setBounds({});
        }

       if (! processor.isOfflineMode())

       {
        placeButtonPair(specSettings.clearOnPlayButton, specSettings.resetClickButton);
       }
       else
       {
        placeButton(specSettings.clearOnPlayButton);
        specSettings.resetClickButton.setBounds({});
       }
        fixedGap.removeFromTop(area);
        if (specHorizontalControls)
            placeButtonPair(specSettings.horizontalZoomButton,
                            specSettings.verticalZoomButton);
        else
        {
            specSettings.horizontalZoomButton.setBounds({});
            placeButton(specSettings.verticalZoomButton);
        }
        fixedGap.removeFromTop(area);
        if (specHorizontalControls)
            placeButtonPair(specSettings.horizontalReadoutsButton,
                            specSettings.verticalReadoutsButton);
        else
        {
            specSettings.horizontalReadoutsButton.setBounds({});
            placeButton(specSettings.verticalReadoutsButton);
        }
        fixedGap.removeFromTop(area);
        placeButtonTriple(specSettings.cursorButton, specSettings.cursorNotesButton,
                          specSettings.cursorVerticalButton);
        fixedGap.removeFromTop(area);
        placeButton(specSettings.monitorControlsButton);

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
        corrSettings.firstGraphColourControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.secondGraphTypeControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.secondGraphColourControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
        corrSettings.graphOpacityControl.setBounds(area.removeFromTop(rowHeight));
        fixedGap.removeFromTop(area);
       if (! processor.isOfflineMode())
       {
        placeButtonPair(corrSettings.clearOnPlayButton, corrSettings.resetClickButton);
       }
       else
       {
        placeButton(corrSettings.clearOnPlayButton);
        corrSettings.resetClickButton.setBounds({});
       }
        fixedGap.removeFromTop(area);
        placeButtonPair(corrSettings.horizontalZoomButton,
                        corrSettings.verticalZoomButton);
        fixedGap.removeFromTop(area);
        placeButtonPair(corrSettings.horizontalReadoutsButton,
                        corrSettings.verticalReadoutsButton);
        fixedGap.removeFromTop(area);
        placeButtonTriple(corrSettings.cursorButton, corrSettings.cursorNotesButton,
                          corrSettings.cursorVerticalButton);
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
   if (! processor.isOfflineMode())
   {
    placeButton(scopSettings.leftToRightButton);
    fixedGap.removeFromTop(area);
   }
   else
   {
    scopSettings.leftToRightButton.setBounds({});
   }
   if (! processor.isOfflineMode())
   {
    scopSettings.horizontalZoomButton.setBounds({});
    placeButton(scopSettings.verticalZoomButton);
   }
   else
   {
    placeButtonPair(scopSettings.horizontalZoomButton, scopSettings.verticalZoomButton);
   }
    fixedGap.removeFromTop(area);
   if (! processor.isOfflineMode())
   {
    scopSettings.horizontalReadoutsButton.setBounds({});
    placeButton(scopSettings.verticalReadoutsButton);
   }
   else
   {
    placeButtonPair(scopSettings.horizontalReadoutsButton, scopSettings.verticalReadoutsButton);
   }
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
    const auto realtimeControlsEnabled = ! processor.isOfflineMode();
    const auto scopPage = analyzerPage == ana::AnalyzerPage::scop;
    const auto specPage = analyzerPage == ana::AnalyzerPage::spec;
    const auto specMapPage = specPage && analyzerViewMode == "MAP";
    const auto specHorizontalControls = ! specMapPage || processor.isOfflineMode();
    const auto corrPage = analyzerPage == ana::AnalyzerPage::corr;
    const auto lvlsPage = analyzerPage == ana::AnalyzerPage::lvls;

    // ARA exposes only controls used by its offline analysis workflow.
    scopSettings.timeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    scopSettings.timeNoteControl.setInteractionEnabled(realtimeControlsEnabled, true);
    scopSettings.timeBaseControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.mapTimeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.mapTimeNoteControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.mapTimeBaseControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.averageTimeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    corrSettings.averageTimeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.firstGraphTypeControl.setInteractionEnabled(true);
    specSettings.secondGraphTypeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.secondGraphColourControl.setInteractionEnabled(realtimeControlsEnabled, true);
    corrSettings.firstGraphTypeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    corrSettings.secondGraphTypeControl.setInteractionEnabled(realtimeControlsEnabled, true);
    corrSettings.secondGraphColourControl.setInteractionEnabled(realtimeControlsEnabled, true);
    lvlsSettings.rmsWindowControl.setInteractionEnabled(realtimeControlsEnabled, true);
    lvlsSettings.peakHoldControl.setInteractionEnabled(realtimeControlsEnabled, true);
    specSettings.clearOnPlayButton.setEnabled(realtimeControlsEnabled);
    specSettings.secondGraphButton.setEnabled(realtimeControlsEnabled);
    corrSettings.clearOnPlayButton.setEnabled(realtimeControlsEnabled);
    lvlsSettings.clearOnPlayButton.setEnabled(realtimeControlsEnabled);
    controlsVisibilityHeadingLabel.setVisible(scopPage || specPage || corrPage);
    lvlsPeakHeadingLabel.setVisible(lvlsPage);
    lvlsLoudnessHeadingLabel.setVisible(lvlsPage);
    lvlsHistoryHeadingLabel.setVisible(lvlsPage);
    for (auto* component : std::array<juce::Component*, 7> {
             &lvlsSettings.loudnessWidthControl, &lvlsSettings.historyRangeHighControl,
             &lvlsSettings.historyRangeLowControl, &lvlsSettings.loudnessClearOnPlayButton,
             &lvlsSettings.loudnessResetClickButton, &lvlsSettings.historyClearOnPlayButton,
             &lvlsSettings.historyResetClickButton })
        component->setVisible(lvlsPage);
    lvlsSettings.loudnessResetClickButton.setVisible(lvlsPage && realtimeControlsEnabled);
    lvlsSettings.historyResetClickButton.setVisible(lvlsPage && realtimeControlsEnabled);
    lvlsSettings.loudnessClearOnPlayButton.setEnabled(realtimeControlsEnabled);
    lvlsSettings.historyClearOnPlayButton.setEnabled(realtimeControlsEnabled);
    for (auto* component : std::array<juce::Component*, 15> {
             &scopSettings.addCrossoverButton, &scopSettings.removeCrossoverButton, &scopSettings.equalHeightButton,
             &scopSettings.styleControl, &scopSettings.opacityControl,
             &scopSettings.horizontalZoomButton, &scopSettings.verticalZoomButton,
             &scopSettings.horizontalReadoutsButton, &scopSettings.verticalReadoutsButton,
             &scopSettings.monitorControlsButton, &scopSettings.toolsButton, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseControl,
             &scopSettings.leftToRightButton })
        component->setVisible(scopPage);
   if (processor.isOfflineMode())
   {
    scopSettings.leftToRightButton.setVisible(false);
   }
   else
   {
    scopSettings.horizontalZoomButton.setVisible(false);
    scopSettings.horizontalReadoutsButton.setVisible(false);
   }
    for (auto& control : scopSettings.crossoverControls)
        control->setVisible(scopPage);
    scopMainHeadingLabel.setVisible(scopPage);

    specSettings.fftSizeControl.setVisible(specPage);
    specSettings.frequencyScaleControl.setVisible(specPage);
    specSettings.mapColourMapControl.setVisible(specMapPage);
    specSettings.fftOverlapControl.setVisible(specPage && ! specMapPage);
    specSettings.mapTimeOverlapControl.setVisible(specMapPage);
    specSettings.mapTimeBaseControl.setVisible(specMapPage);
    specSettings.slopeControl.setVisible(specPage);
    specSettings.rangeLowControl.setVisible(specMapPage);
    specSettings.rangeHighControl.setVisible(specMapPage);
    specSettings.highQualityRenderingButton.setVisible(specMapPage);
    specSettings.mapLeftToRightButton.setVisible(specMapPage && realtimeControlsEnabled);
    specSettings.clearOnPlayButton.setVisible(specPage);
    specSettings.resetClickButton.setVisible(specPage && realtimeControlsEnabled);
    specSettings.horizontalReadoutsButton.setVisible(specPage && specHorizontalControls);
    specSettings.verticalReadoutsButton.setVisible(specPage);
    specSettings.cursorButton.setVisible(specPage);
    specSettings.cursorNotesButton.setVisible(specPage);
    specSettings.cursorVerticalButton.setVisible(specPage);
    specSettings.monitorControlsButton.setVisible(specPage);
    specSettings.horizontalZoomButton.setVisible(specPage && specHorizontalControls);
    specSettings.verticalZoomButton.setVisible(specPage);

    const auto mapNoteTime = processor.isSpecMapTimeNoteBased();
    specSettings.mapTimeControl.setVisible(specMapPage && ! mapNoteTime);
    specSettings.mapTimeNoteControl.setVisible(specMapPage && mapNoteTime);
    if (mapNoteTime && focusedParameterTarget == &specSettings.mapTimeControl)
        clearFocusedParameterControl();

    // Hide FREQ-only line controls in MAP; they do not participate in raster rendering.
    const auto showFreqOnlySpecSettings = specPage && ! specMapPage;
    specSettings.averageTimeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.smoothingControl.setVisible(showFreqOnlySpecSettings);
    specSettings.filledDisplayButton.setVisible(showFreqOnlySpecSettings);
    specSettings.secondGraphButton.setVisible(showFreqOnlySpecSettings);
    specSettings.firstGraphTypeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.firstGraphColourControl.setVisible(showFreqOnlySpecSettings);
    specSettings.secondGraphTypeControl.setVisible(showFreqOnlySpecSettings);
    specSettings.secondGraphColourControl.setVisible(showFreqOnlySpecSettings);
    specSettings.graphOpacityControl.setVisible(showFreqOnlySpecSettings);
    specSettings.antiAliasButton.setVisible(showFreqOnlySpecSettings);

    for (auto* component : std::array<juce::Component*, 20> {
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.firstGraphTypeControl,
             &corrSettings.firstGraphColourControl, &corrSettings.secondGraphTypeControl,
             &corrSettings.secondGraphColourControl, &corrSettings.graphOpacityControl,
             &corrSettings.filledDisplayButton, &corrSettings.secondGraphButton,
             &corrSettings.clearOnPlayButton, &corrSettings.resetClickButton,
             &corrSettings.horizontalReadoutsButton,
             &corrSettings.verticalReadoutsButton, &corrSettings.cursorButton, &corrSettings.cursorNotesButton,
             &corrSettings.cursorVerticalButton,
             &corrSettings.horizontalZoomButton, &corrSettings.verticalZoomButton })
        component->setVisible(corrPage);
    corrSettings.resetClickButton.setVisible(corrPage && realtimeControlsEnabled);
    corrSettings.frequencyScaleControl.setVisible(corrPage);

    for (auto* component : std::array<juce::Component*, 19> {
             &lvlsSettings.widthControl, &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.resetClickButton,
             &lvlsSettings.peakRmsVisibleButton,
             &lvlsSettings.loudnessVisibleButton, &lvlsSettings.historyVisibleButton,
             &lvlsSettings.historyMomentaryButton, &lvlsSettings.historyShortTermButton,
             &lvlsSettings.historyIntegratedButton, &lvlsSettings.historyHorizontalZoomButton,
             &lvlsSettings.historyVerticalZoomButton,
             &lvlsSettings.historyHorizontalReadoutsButton, &lvlsSettings.historyVerticalReadoutsButton })
        component->setVisible(lvlsPage);
    lvlsSettings.resetClickButton.setVisible(lvlsPage && realtimeControlsEnabled);
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
    specSettings.mapTimeControl.commitPendingEditor();
    specSettings.mapTimeNoteControl.commitPendingEditor();
    specSettings.mapTimeBaseControl.commitPendingEditor();
    specSettings.mapColourMapControl.commitPendingEditor();
    specSettings.fftSizeControl.commitPendingEditor();
    specSettings.fftOverlapControl.commitPendingEditor();
    specSettings.mapTimeOverlapControl.commitPendingEditor();
    specSettings.averageTimeControl.commitPendingEditor();
    specSettings.smoothingControl.commitPendingEditor();
    specSettings.firstGraphTypeControl.commitPendingEditor();
    specSettings.firstGraphColourControl.commitPendingEditor();
    specSettings.secondGraphTypeControl.commitPendingEditor();
    specSettings.secondGraphColourControl.commitPendingEditor();
    specSettings.graphOpacityControl.commitPendingEditor();
    specSettings.slopeControl.commitPendingEditor();
    specSettings.rangeLowControl.commitPendingEditor();
    specSettings.rangeHighControl.commitPendingEditor();
    corrSettings.fftSizeControl.commitPendingEditor();
    corrSettings.fftOverlapControl.commitPendingEditor();
    corrSettings.averageTimeControl.commitPendingEditor();
    corrSettings.smoothingControl.commitPendingEditor();
    corrSettings.firstGraphColourControl.commitPendingEditor();
    corrSettings.secondGraphColourControl.commitPendingEditor();
    corrSettings.graphOpacityControl.commitPendingEditor();
    lvlsSettings.widthControl.commitPendingEditor();
    lvlsSettings.peakRangeHighControl.commitPendingEditor();
    lvlsSettings.peakRangeLowControl.commitPendingEditor();
    lvlsSettings.rmsWindowControl.commitPendingEditor();
    lvlsSettings.peakHoldControl.commitPendingEditor();
    lvlsSettings.loudnessRangeHighControl.commitPendingEditor();
    lvlsSettings.loudnessRangeLowControl.commitPendingEditor();
    lvlsSettings.loudnessWidthControl.commitPendingEditor();
    lvlsSettings.historyRangeHighControl.commitPendingEditor();
    lvlsSettings.historyRangeLowControl.commitPendingEditor();

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
