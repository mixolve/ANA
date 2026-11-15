#include "SettingsPanel.h"
#include "shared/animation/View.h"
#include "Processor.h"
#include "Theme.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/SubmoduleParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
constexpr float minimumCrossoverGapHz = 1.0f;
constexpr std::array<const char*,7> sectionKeys { "spec.freq", "spec.map", "corr.signed", "corr.phase", "corr.freq", "lvls", "scop" };
constexpr std::array<const char*,7> sectionTitles { "FREQ", "MAP", "CORR SIGNED", "CORR PHASE", "CORR FREQ", "LVLS", "SCOP" };

double parseFrequency(const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const auto multiplier = trimmed.containsChar('k') ? 1000.0 : 1.0;
    return trimmed.getDoubleValue() * multiplier;
}
}

void SettingsPanel::BlockContent::paint(juce::Graphics& g)
{
    const auto draw=[&](juce::Rectangle<int> frame, juce::Rectangle<int> header, const juce::String& title, bool hover)
    {
        if(frame.isEmpty()) return;
        const auto colour=hover?ana::ui::light:ana::ui::white;
        g.setColour(colour); g.drawRect(frame,2); g.fillRect(header);
        g.setColour(ana::ui::background); g.setFont(ana::ui::makeFont());
        g.drawText(title,header.reduced(8,0),juce::Justification::centred,false);
    };
    draw(unitFrame,unitHeader,"UNIT",hovered==1 && hoverReady);
    draw(moduleFrame,moduleHeader,"MODULE",hovered==2 && hoverReady);
    if (!mainFrame.isEmpty())
    {
        const bool hot = hovered == 10 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(mainFrame, 2);
        g.fillRect(mainHeader);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText("MAIN", mainHeader.reduced(8, 0), juce::Justification::centred, false);
    }
    if (!animationFrame.isEmpty())
    {
        const bool hot = hovered == 14 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(animationFrame, 2);
        g.fillRect(animationHeader);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText("ANIMATION", animationHeader.reduced(8, 0), juce::Justification::centred, false);
    }
    if (!snapshotsFrame.isEmpty())
    {
        const bool hot = hovered == 13 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(snapshotsFrame, 2);
        g.fillRect(snapshotsHeader);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText("SNAPSHOTS", snapshotsHeader.reduced(8, 0), juce::Justification::centred, false);
    }
    if (!oscFrame.isEmpty())
    {
        const bool hot = hovered == 11 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(oscFrame, 2);
        g.fillRect(oscHeader);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText("OSC", oscHeader.reduced(8, 0), juce::Justification::centred, false);
    }
    if (!slotsFrame.isEmpty())
    {
        const bool hot = hovered == 12 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(slotsFrame, 2);
        g.fillRect(slotsHeader);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText("SLOTS", slotsHeader.reduced(8, 0), juce::Justification::centred, false);
    }
    for(size_t i=0;i<sectionTitles.size();++i)
    {
        const auto frame=innerFrames[i], header=innerHeaders[i];
        if(frame.isEmpty()) continue;
        // A section has a #444444 title tile and frame, not a filled content panel.
        // After 50 ms the entire title inverts, as on other interactive controls.
        const bool hot = hovered == static_cast<int>(i) + 3 && hoverReady;
        g.setColour(hot ? ana::ui::light : ana::ui::dark);
        g.drawRect(frame, 2);
        g.fillRect(header);
        g.setColour(hot ? ana::ui::background : ana::ui::white);
        g.setFont(ana::ui::makeFont());
        g.drawText(sectionTitles[i],header.reduced(8,0),juce::Justification::centred,false);
    }
}
void SettingsPanel::BlockContent::mouseMove(const juce::MouseEvent& e)
{
    auto next=unitHeader.contains(e.getPosition())?1:moduleHeader.contains(e.getPosition())?2:0;
    if(mainHeader.contains(e.getPosition())) next=10;
    if(animationHeader.contains(e.getPosition())) next=14;
    if(snapshotsHeader.contains(e.getPosition())) next=13;
    if(oscHeader.contains(e.getPosition())) next=11;
    if(slotsHeader.contains(e.getPosition())) next=12;
    for(size_t i=0;i<innerHeaders.size();++i) if(innerHeaders[i].contains(e.getPosition())) next=int(i)+3;
    if(next==hovered) return;
    hovered=next; hoverReady=false;
    const auto generation=++hoverGeneration;
    if(next!=0) juce::Timer::callAfterDelay(50,[safe=juce::Component::SafePointer<BlockContent>(this),next,generation]
    {
        if(safe==nullptr || safe->hoverGeneration!=generation || safe->hovered!=next) return;
        safe->hoverReady=true; safe->repaint();
    });
    repaint();
}
void SettingsPanel::BlockContent::mouseExit(const juce::MouseEvent&)
{ hovered=0; hoverReady=false; ++hoverGeneration; repaint(); }
void SettingsPanel::BlockContent::mouseDown(const juce::MouseEvent& e)
{
    disabledMarkTarget=nullptr;
    if(e.mods.isCtrlDown() && !e.mods.isShiftDown() && !e.mods.isAltDown() && !e.mods.isCommandDown()
       && (e.mods.isLeftButtonDown() || e.mods.isRightButtonDown()))
    {
        for(auto* child:getChildren())
            if(auto* button=dynamic_cast<ControlButton*>(child);button!=nullptr && button->isVisible()
               && !button->isEnabled() && button->getBounds().contains(e.getPosition()) && button->onMark)
            { disabledMarkTarget=button; pressed=-1; return; }
    }
    pressed=e.mods.isLeftButtonDown()?(unitHeader.contains(e.getPosition())?1:moduleHeader.contains(e.getPosition())?2:0):0;
    if(e.mods.isLeftButtonDown() && mainHeader.contains(e.getPosition())) pressed=10;
    if(e.mods.isLeftButtonDown() && animationHeader.contains(e.getPosition())) pressed=14;
    if(e.mods.isLeftButtonDown() && snapshotsHeader.contains(e.getPosition())) pressed=13;
    if(e.mods.isLeftButtonDown() && oscHeader.contains(e.getPosition())) pressed=11;
    if(e.mods.isLeftButtonDown() && slotsHeader.contains(e.getPosition())) pressed=12;
    if(e.mods.isLeftButtonDown()) for(size_t i=0;i<innerHeaders.size();++i) if(innerHeaders[i].contains(e.getPosition())) pressed=int(i)+3;
}
void SettingsPanel::BlockContent::mouseUp(const juce::MouseEvent& e)
{
    if(pressed==-1)
    {
        if(disabledMarkTarget!=nullptr && !e.mouseWasDraggedSinceMouseDown()
           && disabledMarkTarget->getBounds().contains(e.getPosition()) && disabledMarkTarget->onMark)
            disabledMarkTarget->onMark();
        disabledMarkTarget=nullptr; pressed=0; repaint(); return;
    }
    auto hit=unitHeader.contains(e.getPosition())?1:moduleHeader.contains(e.getPosition())?2:0;
    if(mainHeader.contains(e.getPosition())) hit=10;
    if(animationHeader.contains(e.getPosition())) hit=14;
    if(snapshotsHeader.contains(e.getPosition())) hit=13;
    if(oscHeader.contains(e.getPosition())) hit=11;
    if(slotsHeader.contains(e.getPosition())) hit=12;
    for(size_t i=0;i<innerHeaders.size();++i) if(innerHeaders[i].contains(e.getPosition())) hit=int(i)+3;
    if(pressed!=0 && pressed==hit && !e.mouseWasDraggedSinceMouseDown() && onToggle) onToggle(pressed);
    pressed=0;
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
    // The split attachment reads the same stored parameter as the analyzer's SPLIT button.
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
    // Only the settings window uses square controls; analyzer edge readouts stay compact.
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

void SettingsPanel::resized()
{
    using namespace ana::ui::square;
    auto viewportBounds=getLocalBounds().reduced(spacing);
    viewportBounds.removeFromTop(spacing);
    settingsViewport.setBounds(viewportBounds);
    for(auto* child:settingsContent.getChildren()) child->setBounds({});
    auto& state=processor.getParameters().state;
    const bool unitClosed=bool(state.getProperty("ana.settings.unit.collapsed",false));
    const bool mainClosed=bool(state.getProperty("ana.settings.unit.main.collapsed",false));
    const bool animationClosed=bool(state.getProperty("ana.settings.unit.animation.collapsed",true));
    const bool snapshotsClosed=bool(state.getProperty("ana.settings.unit.snapshots.collapsed",false));
    const bool oscClosed=bool(state.getProperty("ana.settings.unit.osc.collapsed",true));
    const bool slotsClosed=bool(state.getProperty("ana.settings.unit.slots.collapsed",true));
    const bool moduleClosed=bool(state.getProperty("ana.settings.module.collapsed",false));
    collapseSignature=juce::String(int(unitClosed))+juce::String(int(mainClosed))+juce::String(int(animationClosed))+juce::String(int(snapshotsClosed))+juce::String(int(oscClosed))+juce::String(int(slotsClosed))+juce::String(int(moduleClosed));
    const auto& names = sectionKeys;
    for(auto* name:names) collapseSignature+=juce::String(int(bool(state.getProperty(juce::Identifier("ana.settings.module."+juce::String(name)+".collapsed"),true))));
    settingsContent.innerFrames={}; settingsContent.innerHeaders={};
    constexpr int inset=10, headerHeight=22;
    const int frameWidth=contentWidth+4*inset;
    // UNIT owns an independent MAIN tile, with the mode switch first and the
    // seven-choice display selector second. No main-window navigation is needed.
    constexpr int unitPadding = inset;
    const int mainTop = headerHeight + unitPadding;
    settingsContent.mainHeader = unitClosed ? juce::Rectangle<int>()
        : juce::Rectangle<int>(inset, mainTop, contentWidth + 2 * inset, headerHeight);
    const int mainHeight = mainClosed ? headerHeight : headerHeight + inset + ana::ui::controlHeight + spacing + 2 * size + spacing + inset;
    settingsContent.mainFrame = unitClosed ? juce::Rectangle<int>()
        : juce::Rectangle<int>(inset, mainTop, contentWidth + 2 * inset, mainHeight);
    const int animationTop=mainTop+mainHeight+spacing;
    const int animationContentHeight=animationContent!=nullptr?animationContent->preferredEmbeddedHeight(contentWidth):0;
    const int animationHeight=animationClosed?headerHeight:headerHeight+inset+animationContentHeight+inset;
    settingsContent.animationHeader=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,animationTop,contentWidth+2*inset,headerHeight);
    settingsContent.animationFrame=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,animationTop,contentWidth+2*inset,animationHeight);
    screensaverControl.setVisible(!unitClosed && !mainClosed);
    if (animationContent!=nullptr)
    {
        animationContent->setVisible(!unitClosed && !animationClosed);
        if (!unitClosed && !animationClosed) animationContent->setBounds(2*inset,animationTop+headerHeight+inset,contentWidth,animationContentHeight);
    }
    const int snapshotsTop = animationTop + animationHeight + spacing;
    const int snapshotsHeight = snapshotsClosed || snapshotsContent == nullptr
        ? headerHeight : headerHeight + inset + snapshotsPreferredHeight + inset;
    settingsContent.snapshotsHeader = unitClosed ? juce::Rectangle<int>()
        : juce::Rectangle<int>(inset, snapshotsTop, contentWidth + 2 * inset, headerHeight);
    settingsContent.snapshotsFrame = unitClosed ? juce::Rectangle<int>()
        : juce::Rectangle<int>(inset, snapshotsTop, contentWidth + 2 * inset, snapshotsHeight);
    if (snapshotsContent != nullptr)
    {
        snapshotsContent->setVisible(!unitClosed && !snapshotsClosed);
        if (!unitClosed && !snapshotsClosed)
            snapshotsContent->setBounds(2 * inset, snapshotsTop + headerHeight + inset,
                                         contentWidth, snapshotsPreferredHeight);
    }
    const int oscTop = snapshotsTop + snapshotsHeight + spacing;
    const int oscHeight=oscClosed?headerHeight:headerHeight+inset+size+inset;
    settingsContent.oscHeader=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,oscTop,contentWidth+2*inset,headerHeight);
    settingsContent.oscFrame=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,oscTop,contentWidth+2*inset,oscHeight);
    oscPanel.setVisible(!unitClosed && !oscClosed);
    if(!unitClosed && !oscClosed) oscPanel.setBounds(2*inset,oscTop+headerHeight+inset,contentWidth,size);
    const int slotsTop=oscTop+oscHeight+spacing;
    const int slotsHeight=slotsClosed?headerHeight:headerHeight+inset+SlotsPanel::contentHeight+inset;
    settingsContent.slotsHeader=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,slotsTop,contentWidth+2*inset,headerHeight);
    settingsContent.slotsFrame=unitClosed?juce::Rectangle<int>():juce::Rectangle<int>(inset,slotsTop,contentWidth+2*inset,slotsHeight);
    slotsPanel.setVisible(!unitClosed && !slotsClosed);
    if(!unitClosed && !slotsClosed) slotsPanel.setBounds(2*inset,slotsTop+headerHeight+inset,contentWidth,SlotsPanel::contentHeight);
    const int unitHeight = unitClosed ? headerHeight : slotsTop + slotsHeight + inset;
    settingsContent.unitFrame={0,0,frameWidth,unitHeight};
    settingsContent.unitHeader=settingsContent.unitFrame.withHeight(headerHeight);
    if (!unitClosed && !mainClosed)
    {
        trackInfo.setBounds(2*inset, mainTop+headerHeight+inset, contentWidth, ana::ui::controlHeight);
        const int controlsTop=trackInfo.getBottom()+spacing;
        modeControl.setBounds(2 * inset, controlsTop, size, size);
        displayControl.setBounds(2 * inset + size + spacing, controlsTop, size, size);
        freezeControl.setBounds(2 * inset + 2 * (size + spacing), controlsTop, size, size);
        compensationControl.setBounds(2 * inset + 3 * (size + spacing), controlsTop, size, size);
        screensaverControl.setBounds(2*inset+4*(size+spacing),controlsTop,size,size);
        dockCountControl.setBounds(2*inset+3*(size+spacing),controlsTop+size+spacing,size,size);
        for (size_t i=0; i<mainActions.size(); ++i)
            if (auto* control=mainActions[i].getComponent())
                control->setBounds(2*inset+(5+int(i))%columns*(size+spacing),controlsTop+(5+int(i))/columns*(size+spacing),size,size);
    }
    const int moduleTop=unitHeight+spacing;
    settingsContent.moduleHeader={0,moduleTop,frameWidth,headerHeight};
    int column=0, y=moduleTop+headerHeight+inset, bottom=y;
    const auto cell=[&]
    {
        const juce::Rectangle<int> bounds(2*inset+column*(size+spacing),y,size,size);
        bottom=juce::jmax(bottom,bounds.getBottom());
        if(++column==columns) { column=0; y+=size+spacing; }
        return bounds;
    };
    int currentSection=0;
    const auto place=[&] (juce::Component& control)
    { if(control.isVisible()) { control.getProperties().set("settingsModule",names[static_cast<size_t>(currentSection)]); control.setBounds(cell()); } };
    const auto group=[&] (std::initializer_list<juce::Component*> controls)
    {
        for(auto* control:controls) place(*control);

    };
    const auto footer=[&] (std::initializer_list<juce::Component*> controls)
    {
        if(column!=0) { column=0; y+=size+spacing; }
        group(controls);
    };
    const auto time=[&] (ParameterControl& milliseconds,ParameterControl& note,ControlButton& mode)
    {
        const auto bounds=cell();
        // Both variants share a cell so host/OSC changes can switch them without a relayout.
        milliseconds.setBounds(bounds); note.setBounds(bounds);
        place(mode);
    };
    if(moduleClosed)
    {
        settingsContent.moduleFrame=settingsContent.moduleHeader;
        settingsContent.setSize(frameWidth,juce::jmax(settingsContent.moduleFrame.getBottom(),settingsViewport.getHeight()));
        settingsContent.repaint();
        return;
    }
    const auto beginInner=[&](int index)
    {
        column=0;
        settingsContent.innerHeaders[static_cast<size_t>(index)]={inset,y,contentWidth+2*inset,headerHeight};
        y+=headerHeight+inset;
    };
    const auto endInner=[&](int index,bool closed)
    {
        const auto header=settingsContent.innerHeaders[static_cast<size_t>(index)];
        const int end=closed?header.getBottom():juce::jmax(header.getBottom()+inset,bottom+inset);
        settingsContent.innerFrames[static_cast<size_t>(index)]={header.getX(),header.getY(),header.getWidth(),end-header.getY()};
        y=end+spacing; column=0; bottom=juce::jmax(bottom,end);
    };
    const auto closed=[&](int index) { return bool(state.getProperty(juce::Identifier("ana.settings.module."+juce::String(names[static_cast<size_t>(index)])+".collapsed"),true)); };
    for(int section=0;section<7;++section)
    {
    currentSection=section;
    beginInner(section);
    if(closed(section)) { endInner(section,true); continue; }
    if(section==5)
    {
        group({ &lvlsSettings.rmsWindowControl,&lvlsSettings.peakHoldControl,
            &lvlsSettings.readoutIntervalControl,
            &lvlsSettings.peakRangeHighControl,&lvlsSettings.peakRangeLowControl,
            
            &lvlsSettings.loudnessRangeHighControl,&lvlsSettings.loudnessRangeLowControl });
        footer({ &lvlsSettings.clearOnPlayButton,&lvlsSettings.resetClickButton });
        footer({ &lvlsSettings.peakRmsVisibleButton,&lvlsSettings.loudnessVisibleButton,
            &lvlsSettings.metricsVisibleButton });
    }
    else if(section==0 || section==1)
    {
        auto& spec=section==0 ? specSettings : specMapSettings;
        // The first slot is always the seven-way MONITOR choice.
        // FREQ has a separate SPLIT toggle immediately after it.
        place(spec.monitorModeControl);
        if(section==1)
        {
            time(spec.mapTimeControl,spec.mapTimeNoteControl,spec.mapTimeBaseButton);
            group({ &spec.frequencyScaleControl,&spec.mapColourMapControl,
                &spec.fftSizeControl,&spec.mapTimeOverlapControl });
            group({ &spec.slopeControl,&spec.rangeHighControl,&spec.rangeLowControl,
                &spec.highQualityRenderingButton,&spec.mapLeftToRightButton });
        }
        else
        {
            place(spec.splitViewButton);
            group({ &spec.frequencyScaleControl,&spec.fftSizeControl,&spec.fftOverlapControl,
                &spec.averageTimeControl,&spec.smoothingControl,&spec.slopeControl });
            group({ &spec.filledDisplayButton,&spec.secondGraphButton,
                &spec.firstGraphTypeControl,&spec.firstGraphColourControl,
                &spec.secondGraphTypeControl,&spec.secondGraphColourControl,&spec.graphOpacityControl });
        }
        if(section==0) group({ &spec.antiAliasButton });
        footer({ &spec.clearOnPlayButton,&spec.resetClickButton });
        footer({ &spec.cursorButton,&spec.cursorVerticalButton,
            &spec.horizontalZoomButton,&spec.verticalZoomButton,
            &spec.horizontalReadoutsButton,&spec.verticalReadoutsButton });
        footer({ &spec.cursorNotesButton });
    }
    else if(section>=2 && section<=4)
    {
        auto& corr=section==2 ? corrSignedSettings : section==3 ? corrSettings : corrFreqSettings;
        group({ &corr.frequencyScaleControl,&corr.fftSizeControl,&corr.fftOverlapControl,
            &corr.averageTimeControl,&corr.smoothingControl });
        group({ &corr.filledDisplayButton,&corr.secondGraphButton,
            &corr.firstGraphTypeControl,&corr.firstGraphColourControl,
            &corr.secondGraphTypeControl,&corr.secondGraphColourControl,&corr.graphOpacityControl });
        footer({ &corr.clearOnPlayButton,&corr.resetClickButton });
        footer({ &corr.cursorButton,&corr.cursorVerticalButton,
            &corr.horizontalZoomButton,&corr.verticalZoomButton,
            &corr.horizontalReadoutsButton,&corr.verticalReadoutsButton });
        footer({ &corr.cursorNotesButton });
    }
    else
    {
        time(scopSettings.timeControl,scopSettings.timeNoteControl,scopSettings.timeBaseButton);
        group({ &scopSettings.slopeControl, &scopSettings.linkedNormalizeButton, &overallFullControl, &scopClearControl,
                &scopSettings.overallMonitorControl });
        if(column!=0) { column=0; y+=size+spacing; }
        place(scopSettings.crossoverCountControl);
        for(auto& control:scopSettings.crossoverControls) place(*control);
        group({ &scopSettings.equalHeightButton,&scopSettings.leftToRightButton,
            &scopSettings.styleControl,&scopSettings.opacityControl });
        // Align the four controls with columns 3-6 in SPEC and CORR.
        if(column!=0) { column=0; y+=size+spacing; }
        cell(); cell();
        group({ &scopSettings.horizontalZoomButton,&scopSettings.verticalZoomButton,
            &scopSettings.horizontalReadoutsButton,&scopSettings.verticalReadoutsButton });
        footer({ &scopSettings.monitorControlsButton,&scopSettings.toolsButton });
    }
    endInner(section,false);
    }
    settingsContent.moduleFrame={0,moduleTop,frameWidth,juce::jmax(headerHeight,bottom-moduleTop+inset)};
    settingsContent.setSize(frameWidth,juce::jmax(settingsContent.moduleFrame.getBottom(),settingsViewport.getHeight()));
    settingsContent.repaint();
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
    // The panel frame is static between layout/hover changes. Repainting the
    // entire tree here made every visible control (and every slot marker)
    // redraw 15 times per second even while nothing changed. Layout changes
    // and individual controls already invalidate their own bounds.
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
    // Old host presets store MODULE/SPEC VIEW/CORR MODE. They stay authoritative;
    // the single DISPLAY control follows them without changing its UI style.
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
        // ABOUT opens a popup, not a parameter or host action: no SL and no dock copy.
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
