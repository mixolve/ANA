#include "SettingsPanel.h"
#include "SettingsSectionNames.h"
#include "shared/animation/View.h"
#include "Processor.h"
#include "Theme.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/SubmoduleParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using ana::settings_detail::sectionKeys;

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}
}

SettingsPanel::SettingsPanel(PluginProcessor& processorRef)
    : processor(processorRef),
      compensationControl(processorRef.getParameters(), PluginProcessor::compensationParameterId,
                          "COMPEN\nSATION", [] (double value) { return juce::String(value, 2); }),
      modeControl(processorRef.getParameters(), PluginProcessor::analysisModeParameterId,
                  "MODE", [] (double value) { return value >= 0.5 ? juce::String("OFFLINE") : juce::String("REALTIME"); }),
      displayControl(processorRef.getParameters(), PluginProcessor::displayChoiceParameterId,
                     "DISPLAY", [] (double value)
                     {
                         constexpr std::array<const char*, 7> names {
                             "SPEC FREQ", "SPEC\nMAP", "CORR SIGNED", "CORR PHASE", "CORR FREQ", "LVLS", "SCOP" };
                         return juce::String(names[static_cast<size_t>(juce::jlimit(0, 6, juce::roundToInt(value)))]);
                     }),
      screensaverControl(processorRef.getParameters(),PluginProcessor::screensaverParameterId,"SCREEN\nSAVER",[](double v) { return v>=0.5?juce::String("ON"):juce::String("OFF"); }),
      dockCountControl(processorRef.getParameters(),PluginProcessor::dockCountParameterId,"DOCK\nCOUNT",[](double value) { return juce::String(juce::roundToInt(value)); }),
      oscPanel(processorRef),
      slotsPanel(processorRef.getHostSlots()),
      scopSettings(processorRef),
      specSettings(processorRef, false),
      specMapSettings(processorRef, true),
      corrSettings(processorRef, 0),
      corrSignedSettings(processorRef, 2),
      corrFreqSettings(processorRef, 1),
      lvlsSettings(processorRef)
{
    getProperties().set("emptyEditorBackground",true);
    setOpaque(true);
    settingsContent.getProperties().set("emptyEditorBackground", true);
    settingsContent.onToggle=[this](int section)
    {
        oscPanel.dismissEditor(); slotsPanel.cancelMove();
    if (animationContent!=nullptr) animationContent->cancelEditors();
        auto& state=processor.getParameters().state;
        const auto key=section==1?juce::Identifier("ana.settings.unit.collapsed")
            :section==10?juce::Identifier("ana.settings.unit.main.collapsed")
            :section==14?juce::Identifier("ana.settings.unit.animation.collapsed")
            :section==13?juce::Identifier("ana.settings.unit.snapshots.collapsed")
            :section==11?juce::Identifier("ana.settings.unit.osc.collapsed")
            :section==12?juce::Identifier("ana.settings.unit.slots.collapsed")
            :section==2?juce::Identifier("ana.settings.module.collapsed")
            :juce::Identifier("ana.settings.module."+juce::String(sectionKeys[static_cast<size_t>(section-3)])+".collapsed");
        state.setProperty(key,!bool(state.getProperty(key,(section>=3 && section<=9)||section==11||section==12||section==14)),nullptr);
        resized(); settingsContent.repaint();
    };
    settingsViewport.getProperties().set("emptyEditorBackground", true);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(false, false, true, false);
    settingsViewport.setWantsKeyboardFocus(false);
    settingsViewport.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(settingsViewport);

    trackInfo.setName("unit.main.track-info");
    trackInfo.setFont(ana::ui::makeFont());
    trackInfo.setJustificationType(juce::Justification::centred);
    trackInfo.setMinimumHorizontalScale(1.0f);
    trackInfo.setColour(juce::Label::textColourId, ana::ui::white);
    trackInfo.setColour(juce::Label::backgroundColourId, ana::ui::dark);
    trackInfo.setColour(juce::Label::outlineColourId, ana::ui::light);
    trackInfo.setDrawBackground(true);
    trackInfo.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    trackInfo.getProperties().set("dockAllowed", false);
    settingsContent.addAndMakeVisible(trackInfo);
    setTrackInfo(processor.getOfflineTrackNumber(), processor.getOfflineTrackName());
    settingsContent.addAndMakeVisible(freezeControl);
    overallFullControl.setName("scop.overall-full");
    scopClearControl.setName("scop.clear");
    settingsContent.addAndMakeVisible(overallFullControl);
    settingsContent.addAndMakeVisible(scopClearControl);
    freezeControl.setClickingTogglesState(true);
    freezeControl.onClick = [this] { if (onFreezeChanged) onFreezeChanged(freezeControl.getToggleState()); };
    overallFullSlider.setRange(0, 1, 1);
    overallFullControl.setSquareStyle([] { return juce::String("OVERALL\nFULL"); });
    overallFullControl.setDiscreteChoiceCount(2);
    overallFullControl.displayTextProvider = [this] { return overallFullSlider.getValue() >= 0.5 ? juce::String("ON") : juce::String("OFF"); };
    overallFullControl.onChoiceClicked = [this] (int) { overallFullSlider.setValue(1.0 - overallFullSlider.getValue(), juce::sendNotificationSync); };
    overallFullSlider.onValueChange = [this] { processor.setScopFullSourceView(overallFullSlider.getValue()>=0.5); };
    scopClearSlider.setRange(0, 1, 1);
    scopClearControl.setSquareStyle([] { return juce::String("CLEAR"); });
    scopClearControl.setDiscreteChoiceCount(2);
    scopClearControl.displayTextProvider = [] { return juce::String("CLEAR"); };
    scopClearControl.onChoiceClicked = [this] (int) { processor.requestScopClear(); };
    scopClearControl.onDiscreteEdited = [this] { scopClearSlider.setValue(0,juce::dontSendNotification); };
    overallFullAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getParameters(),PluginProcessor::scopOverallFullParameterId,overallFullSlider);
    scopClearAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getParameters(),PluginProcessor::scopClearParameterId,scopClearSlider);
    overallFullControl.bindHostSlots(&processor.getHostSlots(),[] { return juce::String(PluginProcessor::scopOverallFullParameterId); });
    scopClearControl.bindHostSlots(&processor.getHostSlots(),[] { return juce::String(PluginProcessor::scopClearParameterId); });
    settingsContent.addAndMakeVisible(compensationControl);
    compensationControl.setFixedBinding(true);
    settingsContent.addAndMakeVisible(screensaverControl);
    settingsContent.addAndMakeVisible(dockCountControl);
    dockCountControl.setFixedBinding(true);
    dockCountControl.setDiscreteSteps(16);
    settingsContent.addAndMakeVisible(oscPanel);
    settingsContent.addAndMakeVisible(slotsPanel);
    screensaverControl.setFixedBinding(true);
    oscPanel.onTextEditingChanged=[this](bool editing) { if(onTextEditingChanged) onTextEditingChanged(editing); };
    settingsContent.addAndMakeVisible(modeControl);
    settingsContent.addAndMakeVisible(displayControl);
    modeControl.setFixedBinding(true);
    displayControl.setFixedBinding(true);
    modeControl.onValueChanged = [this]
    {
        if (processor.isOfflineMode()) processor.forceOfflineRefresh();
        else processor.cancelOfflineAnalysis();
        if (onAnalysisModeChanged) onAnalysisModeChanged();
    };
    displayControl.onUserChoiceChanged = [this]
    {
        if (syncingDisplayChoice) return;
        const int index = juce::jlimit(0, 6, juce::roundToInt(displayControl.getSlider().getValue()));
        pendingDisplayChoice = index;
        // A mode change may close the choice popup, so finish its click first.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SettingsPanel>(this), index]
        {
            if (safe == nullptr || safe->pendingDisplayChoice != index) return;
            safe->pendingDisplayChoice = -1;
            if (safe->onDisplaySelected) safe->onDisplaySelected(index);
            safe->refreshExternalState();
        });
    };

    for (auto* component : std::initializer_list<juce::Component*> {
             &scopSettings.addCrossoverButton, &scopSettings.removeCrossoverButton, &scopSettings.equalHeightButton, &scopSettings.crossoverCountControl,
             &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.slopeControl, &scopSettings.linkedNormalizeButton,
             &scopSettings.horizontalZoomButton, &scopSettings.verticalZoomButton,
             &scopSettings.horizontalReadoutsButton, &scopSettings.verticalReadoutsButton,
             &scopSettings.overallMonitorControl, &scopSettings.monitorControlsButton, &scopSettings.toolsButton, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.timeBaseButton, &scopSettings.leftToRightButton,
             &specSettings.monitorModeControl, &specSettings.splitViewButton, &specSettings.fftSizeControl,
             &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.mapTimeControl, &specSettings.mapTimeNoteControl,
             &specSettings.mapTimeBaseButton,
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
             &specSettings.horizontalZoomButton,
             &specSettings.verticalZoomButton,
             &specMapSettings.monitorModeControl, &specMapSettings.fftSizeControl,
             &specMapSettings.fftOverlapControl, &specMapSettings.mapTimeOverlapControl,
             &specMapSettings.mapTimeControl, &specMapSettings.mapTimeNoteControl,
             &specMapSettings.mapTimeBaseButton,
             &specMapSettings.averageTimeControl, &specMapSettings.smoothingControl,
             &specMapSettings.frequencyScaleControl, &specMapSettings.mapColourMapControl,
             &specMapSettings.filledDisplayButton, &specMapSettings.secondGraphButton,
             &specMapSettings.firstGraphTypeControl, &specMapSettings.firstGraphColourControl,
             &specMapSettings.secondGraphTypeControl, &specMapSettings.secondGraphColourControl,
             &specMapSettings.graphOpacityControl,
             &specMapSettings.antiAliasButton, &specMapSettings.highQualityRenderingButton, &specMapSettings.mapLeftToRightButton,
             &specMapSettings.slopeControl,
             &specMapSettings.rangeLowControl, &specMapSettings.rangeHighControl,
             &specMapSettings.clearOnPlayButton, &specMapSettings.resetClickButton,
             &specMapSettings.horizontalReadoutsButton,
             &specMapSettings.verticalReadoutsButton, &specMapSettings.cursorButton,
             &specMapSettings.cursorNotesButton, &specMapSettings.cursorVerticalButton,
             &specMapSettings.horizontalZoomButton,
             &specMapSettings.verticalZoomButton,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl, &corrSettings.firstGraphColourControl,
             &corrSettings.secondGraphTypeControl, &corrSettings.secondGraphColourControl,
             &corrSettings.graphOpacityControl,
             &corrSettings.filledDisplayButton, &corrSettings.secondGraphButton,
             &corrSettings.clearOnPlayButton, &corrSettings.resetClickButton,
             &corrSettings.horizontalReadoutsButton, &corrSettings.verticalReadoutsButton,
             &corrSettings.cursorButton, &corrSettings.cursorNotesButton, &corrSettings.cursorVerticalButton, &corrSettings.horizontalZoomButton,
             &corrSettings.verticalZoomButton,             
             &corrSignedSettings.fftSizeControl, &corrSignedSettings.fftOverlapControl, &corrSignedSettings.averageTimeControl,
             &corrSignedSettings.smoothingControl, &corrSignedSettings.frequencyScaleControl,
             &corrSignedSettings.firstGraphTypeControl, &corrSignedSettings.firstGraphColourControl,
             &corrSignedSettings.secondGraphTypeControl, &corrSignedSettings.secondGraphColourControl,
             &corrSignedSettings.graphOpacityControl,
             &corrSignedSettings.filledDisplayButton, &corrSignedSettings.secondGraphButton,
             &corrSignedSettings.clearOnPlayButton, &corrSignedSettings.resetClickButton,
             &corrSignedSettings.horizontalReadoutsButton, &corrSignedSettings.verticalReadoutsButton,
             &corrSignedSettings.cursorButton, &corrSignedSettings.cursorNotesButton, &corrSignedSettings.cursorVerticalButton, &corrSignedSettings.horizontalZoomButton,
             &corrSignedSettings.verticalZoomButton,             
             &corrFreqSettings.fftSizeControl, &corrFreqSettings.fftOverlapControl, &corrFreqSettings.averageTimeControl,
             &corrFreqSettings.smoothingControl, &corrFreqSettings.frequencyScaleControl,
             &corrFreqSettings.firstGraphTypeControl, &corrFreqSettings.firstGraphColourControl,
             &corrFreqSettings.secondGraphTypeControl, &corrFreqSettings.secondGraphColourControl,
             &corrFreqSettings.graphOpacityControl,
             &corrFreqSettings.filledDisplayButton, &corrFreqSettings.secondGraphButton,
             &corrFreqSettings.clearOnPlayButton, &corrFreqSettings.resetClickButton,
             &corrFreqSettings.horizontalReadoutsButton, &corrFreqSettings.verticalReadoutsButton,
             &corrFreqSettings.cursorButton, &corrFreqSettings.cursorNotesButton, &corrFreqSettings.cursorVerticalButton, &corrFreqSettings.horizontalZoomButton,
             &corrFreqSettings.verticalZoomButton, 
             &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.readoutIntervalControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl,
             &lvlsSettings.clearOnPlayButton, &lvlsSettings.resetClickButton,
             
             &lvlsSettings.peakRmsVisibleButton, &lvlsSettings.loudnessVisibleButton,
             &lvlsSettings.metricsVisibleButton })
        settingsContent.addAndMakeVisible(*component);

    // Every visible submodule tile must bind its own explicit APVTS parameter,
    // not silently follow the active FREQ/MAP/PHASE/SIGNED selection.
    for (auto* child : settingsContent.getChildren())
        if (auto* control = dynamic_cast<ParameterControl*>(child))
        {
            const auto id = control->getSourceParameterId();
            if (id.startsWith("spec/") || id.startsWith("corr/"))
                control->setFixedBinding(true);
        }

    scopSettings.addCrossoverButton.onClick = [this] { changeCrossoverCount(1); };
    scopSettings.removeCrossoverButton.onClick = [this] { changeCrossoverCount(-1); };
    scopSettings.equalHeightAttachment=std::make_unique<SubmoduleButtonAttachment>(processor.getParameters(),PluginProcessor::scopEqualHeightParameterId,scopSettings.equalHeightButton,true);
    scopSettings.equalHeightButton.setClickingTogglesState(true);
    scopSettings.equalHeightButton.onClick = [this]
    {
        processor.setScopBandsEqualHeight(scopSettings.equalHeightButton.getToggleState());
        if (onScopBandLayoutChanged) onScopBandLayoutChanged();
    };
    const auto displaySettingChanged = [this]
    {
        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    scopSettings.crossoverCountControl.setDiscreteSteps(6);
    scopSettings.crossoverCountControl.onValueChanged = [this]
    {
        for(size_t index=0;index<processor.getCrossoverCount();++index) constrainFrequency(index);
        refreshExternalState();
        if(onScopBandLayoutChanged) onScopBandLayoutChanged();
    };
    scopSettings.overallMonitorControl.onValueChanged = displaySettingChanged;
    scopSettings.styleControl.onValueChanged = displaySettingChanged;
    scopSettings.opacityControl.onValueChanged = displaySettingChanged;
    scopSettings.linkedNormalizeButton.setClickingTogglesState(true);
    scopSettings.linkedNormalizeButton.setTooltip("NORMALIZE ALL BANDS BY THE LOUDEST PEAK");
    scopSettings.linkedNormalizeButton.getProperties().set("oscParameterId",
        PluginProcessor::scopLinkedNormalizeParameterId);
    scopSettings.linkedNormalizeButton.onClick = [this]
    {
        if (onScopLinkedNormalizeChanged)
            onScopLinkedNormalizeChanged(scopSettings.linkedNormalizeButton.getToggleState());
        refreshExternalState();
    };
    scopSettings.slopeControl.onValueChanged = [this]
    {
        if (onScopSlopeChanged)
            onScopSlopeChanged();
    };
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
    const auto setupSpecButtons = [this, &displaySettingChanged](SpecSettingsSection& section)
    {
        for (auto* button : std::initializer_list<ControlButton*> {
            &section.filledDisplayButton, &section.secondGraphButton,
            &section.antiAliasButton, &section.highQualityRenderingButton,
            &section.mapLeftToRightButton, &section.clearOnPlayButton,
            &section.resetClickButton, &section.horizontalReadoutsButton,
            &section.verticalReadoutsButton, &section.cursorButton,
            &section.cursorNotesButton, &section.cursorVerticalButton,
            &section.horizontalZoomButton,
            &section.verticalZoomButton })
        {
            button->setClickingTogglesState(true);
            button->onClick = displaySettingChanged;
        }
        auto attach = [this, &section](std::unique_ptr<SubmoduleButtonAttachment>& slot,
                                       const char* id, ControlButton& button)
        {
            slot = std::make_unique<SubmoduleButtonAttachment>(processor.getParameters(),
                ana::submodules::selectedId(id,section.isMap,0),button,true);
        };
        attach(section.filledDisplayAttachment,PluginProcessor::specFilledDisplayParameterId,section.filledDisplayButton);
        attach(section.secondGraphAttachment,PluginProcessor::specSecondGraphParameterId,section.secondGraphButton);
        attach(section.antiAliasAttachment,PluginProcessor::specAntiAliasParameterId,section.antiAliasButton);
        attach(section.highQualityRenderingAttachment,PluginProcessor::specMapHighQualityRenderingParameterId,section.highQualityRenderingButton);
        attach(section.mapLeftToRightAttachment,PluginProcessor::specMapLeftToRightParameterId,section.mapLeftToRightButton);
        attach(section.horizontalReadoutsAttachment,PluginProcessor::specHorizontalReadoutsParameterId,section.horizontalReadoutsButton);
        attach(section.verticalReadoutsAttachment,PluginProcessor::specVerticalReadoutsParameterId,section.verticalReadoutsButton);
        attach(section.clearOnPlayAttachment,PluginProcessor::specClearOnPlayParameterId,section.clearOnPlayButton);
        attach(section.resetClickAttachment,PluginProcessor::specResetClickParameterId,section.resetClickButton);
        attach(section.cursorAttachment,PluginProcessor::specCursorReadoutParameterId,section.cursorButton);
        attach(section.cursorNotesAttachment,PluginProcessor::specCursorNotesParameterId,section.cursorNotesButton);
        attach(section.cursorVerticalAttachment,PluginProcessor::specCursorVerticalReadoutParameterId,section.cursorVerticalButton);
        if (!section.isMap)
        {
            section.splitViewButton.setClickingTogglesState(true);
            section.splitViewButton.onClick = displaySettingChanged;
            attach(section.splitViewAttachment, PluginProcessor::specSplitViewParameterId, section.splitViewButton);
        }
        attach(section.horizontalZoomAttachment,PluginProcessor::specHorizontalZoomParameterId,section.horizontalZoomButton);
        attach(section.verticalZoomAttachment,PluginProcessor::specVerticalZoomParameterId,section.verticalZoomButton);
        section.highQualityRenderingButton.setTooltip("Accurate max-bilinear interpolation of a spectrogram (recommended)");
        section.mapLeftToRightButton.setTooltip("Draw REALTIME SPEC MAP left to right");
    };
    setupSpecButtons(specSettings);
    setupSpecButtons(specMapSettings);
    specSettings.splitViewButton.setTooltip("SPLIT FREQ INTO L/R OR M/S PANES");
    for (auto* section : { &specSettings, &specMapSettings })
        section->monitorModeControl.onValueChanged = displaySettingChanged;

    const auto setupCorrButtons = [this, &displaySettingChanged](CorrSettingsSection& section)
    {
        for(auto* button : std::initializer_list<ControlButton*> {
            &section.filledDisplayButton, &section.secondGraphButton, &section.clearOnPlayButton,
            &section.resetClickButton, &section.horizontalReadoutsButton,
            &section.verticalReadoutsButton, &section.cursorButton,
            &section.cursorNotesButton, &section.cursorVerticalButton,
            &section.horizontalZoomButton, &section.verticalZoomButton })
        {
            button->setClickingTogglesState(true);
            button->onClick = displaySettingChanged;
        }
        auto attach = [this, &section](std::unique_ptr<SubmoduleButtonAttachment>& slot,
                                       const char* id, ControlButton& button)
        {
            slot = std::make_unique<SubmoduleButtonAttachment>(processor.getParameters(),
                ana::submodules::selectedId(id,false,section.submoduleMode),button,true);
        };
        attach(section.filledDisplayAttachment,PluginProcessor::corrFilledDisplayParameterId,section.filledDisplayButton);
        attach(section.secondGraphAttachment,PluginProcessor::corrSecondGraphParameterId,section.secondGraphButton);
        attach(section.clearOnPlayAttachment,PluginProcessor::corrClearOnPlayParameterId,section.clearOnPlayButton);
        attach(section.resetClickAttachment,PluginProcessor::corrResetClickParameterId,section.resetClickButton);
        attach(section.horizontalReadoutsAttachment,PluginProcessor::corrHorizontalReadoutsParameterId,section.horizontalReadoutsButton);
        attach(section.verticalReadoutsAttachment,PluginProcessor::corrVerticalReadoutsParameterId,section.verticalReadoutsButton);
        attach(section.cursorAttachment,PluginProcessor::corrCursorReadoutParameterId,section.cursorButton);
        attach(section.cursorNotesAttachment,PluginProcessor::corrCursorNotesParameterId,section.cursorNotesButton);
        attach(section.cursorVerticalAttachment,PluginProcessor::corrCursorVerticalReadoutParameterId,section.cursorVerticalButton);
        attach(section.horizontalZoomAttachment,PluginProcessor::corrHorizontalZoomParameterId,section.horizontalZoomButton);
        attach(section.verticalZoomAttachment,PluginProcessor::corrVerticalZoomParameterId,section.verticalZoomButton);
    };
    setupCorrButtons(corrSettings);
    setupCorrButtons(corrSignedSettings);
    setupCorrButtons(corrFreqSettings);
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
             std::pair { &lvlsSettings.metricsVisibleButton, PluginProcessor::lvlsMetricsVisibleParameterId } })
    {
        button->setClickingTogglesState(true);
        button->onClick = [this, displaySettingChanged, button, parameterId]
        {
            const auto anyVisible = lvlsSettings.peakRmsVisibleButton.getToggleState()
                || lvlsSettings.loudnessVisibleButton.getToggleState()
                || lvlsSettings.metricsVisibleButton.getToggleState();
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
    lvlsSettings.metricsVisibleAttachment = std::make_unique<SubmoduleButtonAttachment>(
        processor.getParameters(), PluginProcessor::lvlsMetricsVisibleParameterId,
        lvlsSettings.metricsVisibleButton);
    const auto requestChoice = [this] (ParameterControl& control)
    {
        if (onChoiceRequested)
            onChoiceRequested(control);
    };
    displayControl.onChoiceRequested = requestChoice;
    specSettings.monitorModeControl.onChoiceRequested = requestChoice;
    specMapSettings.monitorModeControl.onChoiceRequested = requestChoice;
    scopSettings.overallMonitorControl.onChoiceRequested = requestChoice;
    scopSettings.styleControl.onChoiceRequested = requestChoice;
    scopSettings.timeNoteControl.onChoiceRequested = requestChoice;

    for (auto* section : { &specSettings, &specMapSettings })
    {
        for (auto* control : { &section->fftSizeControl, &section->mapTimeOverlapControl,
            &section->mapTimeNoteControl, &section->frequencyScaleControl,
            &section->mapColourMapControl, &section->firstGraphTypeControl,
            &section->firstGraphColourControl, &section->secondGraphTypeControl,
            &section->secondGraphColourControl })
            control->onChoiceRequested = requestChoice;
    }
    for (auto* section : { &corrSettings, &corrSignedSettings, &corrFreqSettings })
    {
        for (auto* control : { &section->fftSizeControl, &section->frequencyScaleControl,
            &section->firstGraphTypeControl, &section->firstGraphColourControl,
            &section->secondGraphTypeControl, &section->secondGraphColourControl })
            control->onChoiceRequested = requestChoice;
    }

    const auto requestReset = [this] (ParameterControl& control)
    {
        if (onResetRequested)
            onResetRequested(control);
    };
    for (auto* control : std::to_array<ParameterControl*>({
             &dockCountControl, &compensationControl, &scopSettings.styleControl, &scopSettings.opacityControl, &scopSettings.slopeControl, &scopSettings.timeControl,
             &scopSettings.timeNoteControl, &scopSettings.overallMonitorControl,
             &specSettings.monitorModeControl, &specSettings.fftSizeControl, &specSettings.fftOverlapControl, &specSettings.mapTimeOverlapControl,
             &specSettings.mapTimeControl, &specSettings.mapTimeNoteControl,
             &specSettings.averageTimeControl,
             &specSettings.smoothingControl, &specSettings.frequencyScaleControl,
             &specSettings.mapColourMapControl,
             &specSettings.firstGraphTypeControl, &specSettings.firstGraphColourControl,
             &specSettings.secondGraphTypeControl, &specSettings.secondGraphColourControl,
             &specSettings.graphOpacityControl, &specSettings.slopeControl,
             &specSettings.rangeLowControl, &specSettings.rangeHighControl,
             &specMapSettings.monitorModeControl, &specMapSettings.fftSizeControl, &specMapSettings.fftOverlapControl, &specMapSettings.mapTimeOverlapControl,
             &specMapSettings.mapTimeControl, &specMapSettings.mapTimeNoteControl,
             &specMapSettings.averageTimeControl,
             &specMapSettings.smoothingControl, &specMapSettings.frequencyScaleControl,
             &specMapSettings.mapColourMapControl,
             &specMapSettings.firstGraphTypeControl, &specMapSettings.firstGraphColourControl,
             &specMapSettings.secondGraphTypeControl, &specMapSettings.secondGraphColourControl,
             &specMapSettings.graphOpacityControl, &specMapSettings.slopeControl,
             &specMapSettings.rangeLowControl, &specMapSettings.rangeHighControl,
             &corrSettings.fftSizeControl, &corrSettings.fftOverlapControl, &corrSettings.averageTimeControl,
             &corrSettings.smoothingControl, &corrSettings.frequencyScaleControl,
             &corrSettings.firstGraphTypeControl, &corrSettings.firstGraphColourControl,
             &corrSettings.secondGraphTypeControl, &corrSettings.secondGraphColourControl,
             &corrSettings.graphOpacityControl,
                         
             &corrSignedSettings.fftSizeControl, &corrSignedSettings.fftOverlapControl, &corrSignedSettings.averageTimeControl,
             &corrSignedSettings.smoothingControl, &corrSignedSettings.frequencyScaleControl,
             &corrSignedSettings.firstGraphTypeControl, &corrSignedSettings.firstGraphColourControl,
             &corrSignedSettings.secondGraphTypeControl, &corrSignedSettings.secondGraphColourControl,
             &corrSignedSettings.graphOpacityControl,
                         
             &corrFreqSettings.fftSizeControl, &corrFreqSettings.fftOverlapControl, &corrFreqSettings.averageTimeControl,
             &corrFreqSettings.smoothingControl, &corrFreqSettings.frequencyScaleControl,
             &corrFreqSettings.firstGraphTypeControl, &corrFreqSettings.firstGraphColourControl,
             &corrFreqSettings.secondGraphTypeControl, &corrFreqSettings.secondGraphColourControl,
             &corrFreqSettings.graphOpacityControl,
              &lvlsSettings.peakRangeHighControl, &lvlsSettings.peakRangeLowControl,
             &lvlsSettings.rmsWindowControl, &lvlsSettings.peakHoldControl,
             &lvlsSettings.readoutIntervalControl,
             &lvlsSettings.loudnessRangeHighControl, &lvlsSettings.loudnessRangeLowControl }))
    {
        control->onResetRequested = requestReset;
        control->onTextEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
    }

    lvlsSettings.peakRangeHighControl.onValueChanged = displaySettingChanged;
    lvlsSettings.peakRangeLowControl.onValueChanged = displaySettingChanged;
    lvlsSettings.rmsWindowControl.onValueChanged = displaySettingChanged;
    lvlsSettings.peakHoldControl.onValueChanged = displaySettingChanged;
    lvlsSettings.readoutIntervalControl.onValueChanged = displaySettingChanged;
    lvlsSettings.loudnessRangeHighControl.onValueChanged = displaySettingChanged;
    lvlsSettings.loudnessRangeLowControl.onValueChanged = displaySettingChanged;
    for (auto* section : { &specSettings, &specMapSettings })
    {
        for (auto* control : { &section->smoothingControl, &section->frequencyScaleControl,
            &section->mapColourMapControl, &section->firstGraphColourControl,
            &section->firstGraphTypeControl, &section->secondGraphColourControl,
            &section->graphOpacityControl, &section->slopeControl,
            &section->rangeLowControl, &section->rangeHighControl })
            control->onValueChanged = displaySettingChanged;
        section->fftOverlapControl.setValueParser([] (const juce::String& text)
        { return text.getDoubleValue() * 0.01; });
    }
    for (auto* section : { &corrSettings, &corrSignedSettings, &corrFreqSettings })
    {
        for (auto* control : { &section->smoothingControl, &section->frequencyScaleControl,
            &section->firstGraphColourControl, &section->secondGraphColourControl,
            &section->graphOpacityControl })
            control->onValueChanged = displaySettingChanged;
        section->fftOverlapControl.setValueParser([] (const juce::String& text)
        { return text.getDoubleValue() * 0.01; });
    }
    scopSettings.timeBaseButton.setTooltip("TIME BASE: TIME / NOTE");
    scopSettings.timeBaseButton.onClick = [this]
    {
        ana::ui::analyzer_detail::setParameterPlainValue(processor,
            PluginProcessor::scopTimeBaseParameterId, processor.isScopTimeNoteBased() ? 0.0f : 1.0f);
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    scopSettings.timeNoteControl.onValueChanged = displaySettingChanged;
    specMapSettings.mapTimeBaseButton.setTooltip("TIME BASE: TIME / NOTE");
    specMapSettings.mapTimeBaseButton.onClick = [this]
    {
        ana::ui::analyzer_detail::setParameterPlainValue(processor,
            PluginProcessor::specMapTimeBaseParameterId, processor.isSpecMapTimeNoteBased() ? 0.0f : 1.0f);
        refreshExternalState();
        resized();

        if (onDisplaySettingsChanged)
            onDisplaySettingsChanged();
    };
    specMapSettings.mapTimeNoteControl.onValueChanged = displaySettingChanged;
    specMapSettings.mapTimeControl.onValueChanged = displaySettingChanged;

    for (size_t index = 0; index < scopSettings.crossoverControls.size(); ++index)
    {
        auto control = std::make_unique<ParameterControl>(
            processor.getParameters(),
            PluginProcessor::crossoverParameterIds[index],
            "SPLIT-" + juce::String(static_cast<int>(index + 1)),
            [] (const double value) { return juce::String(juce::roundToInt(value)); });
        control->setValueParser([] (const juce::String& text)
        {
            return parseFrequency(text);
        });
        control->onValueChanged = [this, index] { constrainFrequency(index); };
        control->onResetRequested = requestReset;
        control->onTextEditingChanged = [this] (const bool isEditing)
        {
            if (onTextEditingChanged)
                onTextEditingChanged(isEditing);
        };
        settingsContent.addAndMakeVisible(*control);
        scopSettings.crossoverControls[index] = std::move(control);
    }

    scopSettings.timeBaseButton.getProperties().set("oscParameterId",PluginProcessor::scopTimeBaseParameterId);
    specMapSettings.mapTimeBaseButton.getProperties().set("oscParameterId",PluginProcessor::specMapTimeBaseParameterId);
    for(auto* child : settingsContent.getChildren()) configureControl(child);
    scopSettings.timeBaseButton.valueTextProvider=[this] { return processor.isScopTimeNoteBased()?juce::String("NOTE"):juce::String("TIME"); };
    specMapSettings.mapTimeBaseButton.valueTextProvider=[this] { return processor.isSpecMapTimeNoteBased()?juce::String("NOTE"):juce::String("TIME"); };
    scopSettings.timeBaseButton.setButtonText("TIME-BASE");
    specMapSettings.mapTimeBaseButton.setButtonText("TIME-BASE");
    scopSettings.timeBaseButton.setClickingTogglesState(true);
    specMapSettings.mapTimeBaseButton.setClickingTogglesState(true);
    settingsViewport.setScrollBarsShown(true,false);
    settingsViewport.setScrollBarThickness(12);

    for (size_t index = 0; index < processor.getCrossoverCount(); ++index)
        constrainFrequency(index);

    refreshExternalState();
    settingsContent.addMouseListener(this, true);
    startTimerHz(15);
}

SettingsPanel::~SettingsPanel()
{
    settingsContent.removeMouseListener(this);
    settingsViewport.setViewedComponent(nullptr, false);
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

    refreshExternalState();
    resized();
    return true;
}

void SettingsPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::square::panelBackground);
    graphics.setColour(ana::ui::background);
    if(!bool(getProperties()["externalSettingsFrame"])) graphics.drawRect(getLocalBounds(),2);
}

void SettingsPanel::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isCtrlDown() || juce::Component::getCurrentlyModalComponent()!=nullptr) return;
    if (event.originalComponent != &settingsContent && event.originalComponent != this)
        return;

    dismissParameterEditors();

}

void SettingsPanel::timerCallback()
{
    refreshExternalState();
    const auto& state=processor.getParameters().state;
    juce::String signature=juce::String(int(bool(state.getProperty("ana.settings.unit.collapsed",false))))
        +juce::String(int(bool(state.getProperty("ana.settings.unit.main.collapsed",false))))
        +juce::String(int(bool(state.getProperty("ana.settings.unit.animation.collapsed",true))))
        +juce::String(int(bool(state.getProperty("ana.settings.unit.snapshots.collapsed",false))))
        +juce::String(int(bool(state.getProperty("ana.settings.unit.osc.collapsed",true))))
        +juce::String(int(bool(state.getProperty("ana.settings.unit.slots.collapsed",true))))
        +juce::String(int(bool(state.getProperty("ana.settings.module.collapsed",false))));
    for(auto* name:sectionKeys)
        signature+=juce::String(int(bool(state.getProperty(juce::Identifier("ana.settings.module."+juce::String(name)+".collapsed"),true))));
    if(signature!=collapseSignature) resized();
    // Child controls invalidate themselves; repainting the whole tree would
    // redraw every slot even when only one control changes.
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
    scopSettings.equalHeightButton.setToggleState(processor.areScopBandsEqualHeight(),juce::dontSendNotification);
    scopSettings.linkedNormalizeButton.setToggleState(processor.isScopLinkedNormalized(),juce::dontSendNotification);
    const bool realtime = !processor.isOfflineMode();
    modeControl.setInteractionEnabled(processor.isOfflineAvailable(), true);
    // DISPLAY mirrors the authoritative module, SPEC view and CORR mode parameters.
    int displayIndex = 0;
    switch (processor.getAnalyzerPageState())
    {
        case ana::AnalyzerPage::spec:
            displayIndex = processor.isSpecMapView() ? 1 : 0;
            break;
        case ana::AnalyzerPage::corr:
        {
            const auto mode = processor.getRawParameterValue(PluginProcessor::corrModeParameterId);
            const int value = mode != nullptr ? juce::roundToInt(mode->load(std::memory_order_relaxed)) : 0;
            displayIndex = value == 2 ? 2 : value == 1 ? 4 : 3;
            break;
        }
        case ana::AnalyzerPage::lvls: displayIndex = 5; break;
        case ana::AnalyzerPage::scop: displayIndex = 6; break;
    }
    if (pendingDisplayChoice < 0 && !displayControl.isDiscreteGestureActive())
        if (auto* parameter = processor.getParameters().getParameter(PluginProcessor::displayChoiceParameterId))
            if (juce::roundToInt(displayControl.getSlider().getValue()) != displayIndex)
            {
                const juce::ScopedValueSetter<bool> guard(syncingDisplayChoice, true);
                parameter->setValue(parameter->convertTo0to1(static_cast<float>(displayIndex)));
            }

    scopSettings.overallMonitorControl.setInteractionEnabled(processor.isScopFullSourceView(), true);
    scopSettings.timeControl.setInteractionEnabled(realtime, true);
    scopSettings.timeNoteControl.setInteractionEnabled(realtime, true);
    scopSettings.timeBaseButton.setInteractionEnabled(realtime);
    scopSettings.leftToRightButton.setInteractionEnabled(realtime);
    scopSettings.horizontalZoomButton.setInteractionEnabled(!realtime);
    scopSettings.horizontalReadoutsButton.setInteractionEnabled(!realtime);
    for(auto* component : std::initializer_list<juce::Component*> {
        &scopSettings.crossoverCountControl, &scopSettings.equalHeightButton,
        &scopSettings.styleControl, &scopSettings.opacityControl,
        &scopSettings.slopeControl, &scopSettings.linkedNormalizeButton,
        &scopSettings.overallMonitorControl,
        &scopSettings.horizontalZoomButton, &scopSettings.verticalZoomButton,
        &scopSettings.horizontalReadoutsButton, &scopSettings.verticalReadoutsButton,
        &scopSettings.monitorControlsButton, &scopSettings.toolsButton,
        &scopSettings.timeBaseButton, &scopSettings.leftToRightButton })
        component->setVisible(true);
    const bool scopNote = processor.isScopTimeNoteBased();
    scopSettings.timeBaseButton.setToggleState(scopNote,juce::dontSendNotification);
    scopSettings.timeControl.setVisible(!scopNote);
    scopSettings.timeNoteControl.setVisible(scopNote);
    scopSettings.addCrossoverButton.setVisible(false);
    scopSettings.removeCrossoverButton.setVisible(false);
    const auto xovers=processor.getCrossoverCount();
    scopSettings.addCrossoverButton.setInteractionEnabled(xovers<scopSettings.crossoverControls.size());
    scopSettings.removeCrossoverButton.setInteractionEnabled(xovers>0);
    for(size_t i=0;i<scopSettings.crossoverControls.size();++i)
    {
        scopSettings.crossoverControls[i]->setVisible(true);
        scopSettings.crossoverControls[i]->setInteractionEnabled(i<xovers,true);
    }

    for(auto* section : { &specSettings, &specMapSettings })
    {
        const bool map=section->isMap;
        section->monitorModeControl.setVisible(true);
        section->fftSizeControl.setVisible(true);
        section->frequencyScaleControl.setVisible(true);
        section->slopeControl.setVisible(true);
        section->fftOverlapControl.setVisible(!map);
        section->mapTimeOverlapControl.setVisible(map);
        section->mapColourMapControl.setVisible(map);
        section->mapTimeBaseButton.setVisible(map);
        section->splitViewButton.setVisible(!map);
        section->splitViewButton.setInteractionEnabled(!map &&
            (juce::roundToInt(section->monitorModeControl.getSlider().getValue()) == 1 ||
             juce::roundToInt(section->monitorModeControl.getSlider().getValue()) == 4));
        const bool mapNote=processor.isSpecMapTimeNoteBased();
        section->mapTimeControl.setVisible(map && !mapNote);
        section->mapTimeNoteControl.setVisible(map && mapNote);
        section->mapTimeBaseButton.setToggleState(mapNote,juce::dontSendNotification);
        section->mapTimeControl.setInteractionEnabled(realtime,true);
        section->mapTimeNoteControl.setInteractionEnabled(realtime,true);
        section->mapTimeBaseButton.setInteractionEnabled(realtime);
        for(auto* component : std::initializer_list<juce::Component*> {
            &section->averageTimeControl, &section->smoothingControl,
            &section->filledDisplayButton, &section->secondGraphButton,
            &section->firstGraphTypeControl, &section->firstGraphColourControl,
            &section->secondGraphTypeControl, &section->secondGraphColourControl,
            &section->graphOpacityControl, &section->antiAliasButton })
            component->setVisible(!map);
        for(auto* component : std::initializer_list<juce::Component*> {
            &section->rangeLowControl, &section->rangeHighControl,
            &section->highQualityRenderingButton, &section->mapLeftToRightButton })
            component->setVisible(map);
        for(auto* component : std::initializer_list<juce::Component*> {
            &section->clearOnPlayButton,&section->resetClickButton,
            &section->horizontalReadoutsButton,&section->verticalReadoutsButton,
            &section->cursorButton,&section->cursorVerticalButton,
            &section->cursorNotesButton,
            &section->horizontalZoomButton,&section->verticalZoomButton })
            component->setVisible(true);
        section->averageTimeControl.setInteractionEnabled(realtime,true);
        section->secondGraphTypeControl.setInteractionEnabled(realtime,true);
        section->secondGraphColourControl.setInteractionEnabled(realtime,true);
        section->secondGraphButton.setInteractionEnabled(realtime);
        section->clearOnPlayButton.setInteractionEnabled(realtime);
        section->resetClickButton.setInteractionEnabled(realtime);
        section->mapLeftToRightButton.setInteractionEnabled(realtime);
        section->horizontalReadoutsButton.setInteractionEnabled(!map || !realtime);
        section->horizontalZoomButton.setInteractionEnabled(!map || !realtime);
    }

    for(auto* section : { &corrSettings, &corrSignedSettings, &corrFreqSettings })
    {
        for(auto* component : std::initializer_list<juce::Component*> {
            &section->fftSizeControl,&section->fftOverlapControl,
            &section->averageTimeControl,&section->smoothingControl,
            &section->frequencyScaleControl,&section->firstGraphTypeControl,
            &section->firstGraphColourControl,&section->secondGraphTypeControl,
            &section->secondGraphColourControl,&section->graphOpacityControl,
            &section->filledDisplayButton,&section->secondGraphButton,
            &section->clearOnPlayButton,&section->resetClickButton,
            &section->horizontalReadoutsButton,&section->verticalReadoutsButton,
            &section->cursorButton,&section->cursorNotesButton,
            &section->cursorVerticalButton,&section->horizontalZoomButton,
            &section->verticalZoomButton })
            component->setVisible(true);
        section->averageTimeControl.setInteractionEnabled(realtime,true);
        section->firstGraphTypeControl.setInteractionEnabled(realtime,true);
        section->secondGraphTypeControl.setInteractionEnabled(realtime,true);
        section->secondGraphColourControl.setInteractionEnabled(realtime,true);
        section->clearOnPlayButton.setInteractionEnabled(realtime);
        section->resetClickButton.setInteractionEnabled(realtime);
        section->secondGraphButton.setInteractionEnabled(realtime);
    }

    for(auto* component : std::initializer_list<juce::Component*> {
         &lvlsSettings.peakRangeHighControl,
        &lvlsSettings.peakRangeLowControl, &lvlsSettings.rmsWindowControl,
        &lvlsSettings.peakHoldControl, &lvlsSettings.readoutIntervalControl,
        &lvlsSettings.loudnessRangeHighControl,&lvlsSettings.loudnessRangeLowControl,
        &lvlsSettings.clearOnPlayButton,
        &lvlsSettings.resetClickButton,&lvlsSettings.peakRmsVisibleButton,
        &lvlsSettings.loudnessVisibleButton,&lvlsSettings.metricsVisibleButton })
        component->setVisible(true);
    lvlsSettings.rmsWindowControl.setInteractionEnabled(realtime,true);
    lvlsSettings.peakHoldControl.setInteractionEnabled(realtime,true);
    lvlsSettings.readoutIntervalControl.setInteractionEnabled(realtime,true);
    lvlsSettings.clearOnPlayButton.setInteractionEnabled(realtime);
    lvlsSettings.resetClickButton.setInteractionEnabled(realtime);
    refreshVisibilityLayout();
}

juce::Identifier SettingsPanel::moduleCollapseKey() const
{
    const auto name=analyzerPage==ana::AnalyzerPage::spec?"spec":analyzerPage==ana::AnalyzerPage::corr?"corr":analyzerPage==ana::AnalyzerPage::lvls?"lvls":"scop";
    return juce::Identifier("ana.settings.module."+juce::String(name)+".collapsed");
}

void SettingsPanel::refreshVisibilityLayout()
{
    const auto& children=settingsContent.getChildren();
    auto changed=layoutVisibility.size()!=static_cast<size_t>(children.size());
    layoutVisibility.resize(static_cast<size_t>(children.size()));
    for(int index=0;index<children.size();++index)
    {
        const auto visible=children[index]->isVisible();
        const auto slot=static_cast<size_t>(index);
        changed=changed || layoutVisibility[slot]!=visible;
        layoutVisibility[slot]=visible;
    }
    if(changed && getWidth()>0 && getHeight()>0) resized();
}

void SettingsPanel::setActionStates(bool frozen, bool fullSource, bool realtime, bool fullSourceEnabled)
{
    freezeControl.setInteractionEnabled(realtime);
    freezeControl.setToggleState(frozen, juce::dontSendNotification);
    overallFullControl.setInteractionEnabled(fullSourceEnabled);
    overallFullSlider.setValue(fullSource ? 1.0 : 0.0, juce::dontSendNotification);
    scopSettings.overallMonitorControl.setInteractionEnabled(fullSource, true);
    compensationControl.setInteractionEnabled(realtime, true);
    scopClearControl.setInteractionEnabled(realtime);
}

void SettingsPanel::dismissParameterEditors()
{
    oscPanel.dismissEditor(); slotsPanel.cancelMove();
    if (animationContent!=nullptr) animationContent->cancelEditors();
    for (auto* child : settingsContent.getChildren())
        if (auto* parameter = dynamic_cast<ParameterControl*>(child))
            parameter->cancelPendingEditor();
}

void SettingsPanel::configureControl(juce::Component* child)
{
        auto keyFor=[child]() -> juce::Identifier
        {
            juce::String identity;
            if(auto* control=dynamic_cast<ParameterControl*>(child))
                identity="parameter:"+control->getParameterId();
            else if(auto* button=dynamic_cast<ControlButton*>(child))
            {
                const auto sourceId=button->getProperties()["sourceParameterId"].toString();
                const auto parameterId=button->getProperties()["oscParameterId"].toString();
                identity=parameterId.isNotEmpty()?"parameter:"+parameterId
                    :"button:"+button->getProperties()["settingsModule"].toString()+":"+button->getButtonText();
            }
            else if(child->getName().isNotEmpty()) identity="action:"+child->getName();
            if(identity.isEmpty()) return {};
            return juce::Identifier("ana.settings.selected."+juce::String::toHexString(identity.toRawUTF8(),int(identity.getNumBytesAsUTF8()),0));
        };
        auto selected=[this,keyFor,child] { const auto key=keyFor(); child->getProperties().set("dockSelectionKey",key.toString()); return key.isValid() && bool(processor.getParameters().state.getProperty(key,false)); };
        auto toggle=[this,keyFor,child]
        {
            const auto key=keyFor(); if(!key.isValid()) return;
            auto& state=processor.getParameters().state;
            if(bool(state.getProperty(key,false))) state.removeProperty(key,nullptr); else state.setProperty(key,true,nullptr);
            auto order=juce::StringArray::fromTokens(state.getProperty("ana.dock.order").toString(),"|","");
            order.removeString(key.toString()); if(bool(state.getProperty(key,false))) order.add(key.toString());
            state.setProperty("ana.dock.order",order.joinIntoString("|"),nullptr);
            child->repaint();
        };
        if(auto* control=dynamic_cast<ParameterControl*>(child)) { control->setSquareStyle(true); control->setMarking(selected,toggle); }
        if(auto* button=dynamic_cast<ControlButton*>(child)) { button->bindHostSlots(&processor.getHostSlots(),[button] { return button->getProperties()["oscParameterId"].toString(); }); button->setSquareStyle(true); button->isMarked=selected; button->onMark=toggle; }
        if(auto* value=dynamic_cast<NumericValueBox*>(child)) { value->isMarked=selected; value->onMark=toggle; }
}

void SettingsPanel::attachMainControl(ControlButton& control, int index, bool allowDock)
{
    jassert(index >= 0 && index < int(mainActions.size()));
    mainActions[static_cast<size_t>(index)] = &control;
    settingsContent.addAndMakeVisible(control);
    if (allowDock)
        configureControl(&control);
    else
    {
        control.setSquareStyle(true);
        control.setSlotIndicatorVisible(false);
        control.isMarked = {};
        control.onMark = {};
        control.getProperties().set("dockAllowed", false);
        control.getProperties().remove(juce::Identifier("dockSelectionKey"));
    }
    resized();
}

void SettingsPanel::attachSnapshotsContent(juce::Component& content, int preferredHeight,
                                           const juce::Array<juce::Component*>& dockControls)
{
    if (snapshotsContent == &content)
    {
        setSnapshotsPreferredHeight(preferredHeight);
        return;
    }
    detachSnapshotsContent();
    snapshotsContent = &content;
    snapshotsPreferredHeight = juce::jmax(64, preferredHeight);
    snapshotDockControls = dockControls;
    for (auto* control : snapshotDockControls)
        if (control != nullptr) configureControl(control);
    settingsContent.addAndMakeVisible(content);
    resized();
}

void SettingsPanel::detachSnapshotsContent()
{
    if (snapshotsContent == nullptr) return;
    snapshotDockControls.clear();
    settingsContent.removeChildComponent(snapshotsContent);
    snapshotsContent = nullptr;
    resized();
}

void SettingsPanel::setSnapshotsPreferredHeight(int preferredHeight)
{
    preferredHeight = juce::jmax(64, preferredHeight);
    if (snapshotsPreferredHeight == preferredHeight) return;
    snapshotsPreferredHeight = preferredHeight;
    if (snapshotsContent != nullptr) resized();
}

void SettingsPanel::setTrackInfo(int trackNumber, const juce::String& trackName)
{
    const auto text=juce::String(trackNumber).paddedLeft('0',3)+": "+trackName;
    if (trackInfo.getText()==text) return;
    trackInfo.setText(text,juce::dontSendNotification);
    trackInfo.setTooltip(text);
}

void SettingsPanel::attachAnimationContent(AnimationSettingsPanel& content)
{
    detachAnimationContent();
    animationContent=&content;
    settingsContent.addAndMakeVisible(content);
    content.onChoiceRequested=[this](ParameterControl& c) { if(onChoiceRequested) onChoiceRequested(c); };
    content.onResetRequested=[this](ParameterControl& c) { if(onResetRequested) onResetRequested(c); };
    for(auto* control:content.parameterControls()) configureControl(control);
    resized();
}
void SettingsPanel::detachAnimationContent()
{
    if (animationContent==nullptr) return;
    settingsContent.removeChildComponent(animationContent);
    animationContent=nullptr;
    resized();
}
void SettingsPanel::openAnimationBlock()
{
    auto& state=processor.getParameters().state;
    state.setProperty("ana.settings.unit.collapsed",false,nullptr);
    state.setProperty("ana.settings.unit.animation.collapsed",false,nullptr);
    resized();
    if (animationContent!=nullptr) settingsViewport.setViewPosition(0,settingsContent.animationHeader.getY());
}

juce::Array<juce::Component*> SettingsPanel::dockSources() const
{
    juce::Array<juce::Component*> result=settingsContent.getChildren();
    if(animationContent!=nullptr) for(auto* control:animationContent->parameterControls()) result.add(control);
    for(auto* control:snapshotDockControls) if(control!=nullptr) result.add(control);
    return result;
}
