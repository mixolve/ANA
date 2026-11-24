#include "SettingsPanel.h"
#include "SettingsSectionNames.h"
#include "shared/animation/View.h"
#include "shell/Processor.h"
#include "Theme.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/SubmoduleParameters.h"

#include <algorithm>
#include <array>
#include <cmath>

using ana::settings_detail::sectionKeys;
using ana::settings_detail::sectionTitles;

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
