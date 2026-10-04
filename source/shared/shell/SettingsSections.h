#pragma once

#include "Controls.h"
#include "ParameterControl.h"
#include "SubmoduleButtonAttachment.h"
#include "shared/scop/Crossover.h"

#include <JuceHeader.h>

#include <array>
#include <memory>

class PluginProcessor;

struct ScopSettingsSection
{
    explicit ScopSettingsSection(PluginProcessor& processor);

    ParameterControl styleControl;
    ParameterControl opacityControl;
    ParameterControl timeControl;
    ParameterControl timeNoteControl;
    ParameterControl timeBaseControl;
    ControlButton addCrossoverButton { "ADD" };
    ControlButton removeCrossoverButton { "DEL" };
    ControlButton equalHeightButton { "EQUAL-HEIGHT" };
    ControlButton horizontalZoomButton { "ZOOM-HORIZ" };
    ControlButton verticalZoomButton { "ZOOM-VERT" };
    ControlButton horizontalReadoutsButton { "RO-HORIZ" };
    ControlButton verticalReadoutsButton { "RO-VERT" };
    ControlButton monitorControlsButton { "MONITOR" };
    ControlButton toolsButton { "TOOLS" };
    ControlButton leftToRightButton { "LEFT-TO-RIGHT" };
    std::unique_ptr<SubmoduleButtonAttachment> horizontalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> toolsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> leftToRightAttachment;
    std::array<std::unique_ptr<ParameterControl>, ana::dsp::LinkwitzRileyCrossover::numCrossovers> crossoverControls;
};

struct SpecSettingsSection
{
    explicit SpecSettingsSection(PluginProcessor& processor);

    ParameterControl fftSizeControl;
    ParameterControl fftOverlapControl;
    ParameterControl mapTimeOverlapControl;
    ParameterControl mapTimeControl;
    ParameterControl mapTimeNoteControl;
    ParameterControl mapTimeBaseControl;
    ParameterControl averageTimeControl;
    ParameterControl smoothingControl;
    ParameterControl frequencyScaleControl;
    ParameterControl mapColourMapControl;
    ParameterControl firstGraphTypeControl;
    ParameterControl firstGraphColourControl;
    ParameterControl secondGraphTypeControl;
    ParameterControl secondGraphColourControl;
    ParameterControl graphOpacityControl;
    ParameterControl slopeControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    ControlButton filledDisplayButton { "FILLED-DISPLAY" };
    ControlButton secondGraphButton { "2ND-GRAPH" };
    ControlButton antiAliasButton { "ANTI-ALIAS" };
    ControlButton highQualityRenderingButton { "HIGH-QUALITY RENDERING" };
    ControlButton mapLeftToRightButton { "LEFT-TO-RIGHT" };
    ControlButton horizontalReadoutsButton { "RO-HORIZ" };
    ControlButton verticalReadoutsButton { "RO-VERT" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton resetClickButton { "RESET-CLICK" };
    ControlButton cursorButton { "CUR-HORIZ" };
    ControlButton cursorNotesButton { "CUR-NOTES" };
    ControlButton cursorVerticalButton { "CUR-VERT" };
    ControlButton monitorControlsButton { "MONITOR" };
    ControlButton horizontalZoomButton { "ZOOM-HORIZ" };
    ControlButton verticalZoomButton { "ZOOM-VERT" };
    std::unique_ptr<SubmoduleButtonAttachment> filledDisplayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> secondGraphAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> antiAliasAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> highQualityRenderingAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> mapLeftToRightAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> resetClickAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorNotesAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorVerticalAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalZoomAttachment;
};

struct CorrSettingsSection
{
    explicit CorrSettingsSection(PluginProcessor& processor);

    ParameterControl fftSizeControl;
    ParameterControl fftOverlapControl;
    ParameterControl averageTimeControl;
    ParameterControl smoothingControl;
    ParameterControl frequencyScaleControl;
    ParameterControl firstGraphTypeControl;
    ParameterControl firstGraphColourControl;
    ParameterControl secondGraphTypeControl;
    ParameterControl secondGraphColourControl;
    ParameterControl graphOpacityControl;
    ControlButton filledDisplayButton { "FILLED-DISPLAY" };
    ControlButton secondGraphButton { "2ND-GRAPH" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton resetClickButton { "RESET-CLICK" };
    ControlButton horizontalReadoutsButton { "RO-HORIZ" };
    ControlButton verticalReadoutsButton { "RO-VERT" };
    ControlButton cursorButton { "CUR-HORIZ" };
    ControlButton cursorNotesButton { "CUR-NOTES" };
    ControlButton cursorVerticalButton { "CUR-VERT" };
    ControlButton horizontalZoomButton { "ZOOM-HORIZ" };
    ControlButton verticalZoomButton { "ZOOM-VERT" };
    std::unique_ptr<SubmoduleButtonAttachment> filledDisplayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> secondGraphAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> resetClickAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorNotesAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> cursorVerticalAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalZoomAttachment;
};

struct LvlsSettingsSection
{
    explicit LvlsSettingsSection(PluginProcessor& processor);

    ParameterControl widthControl;
    ParameterControl peakRangeHighControl;
    ParameterControl peakRangeLowControl;
    ParameterControl rmsWindowControl;
    ParameterControl peakHoldControl;
    ParameterControl loudnessRangeHighControl;
    ParameterControl loudnessRangeLowControl;
    ParameterControl loudnessWidthControl;
    ParameterControl historyRangeHighControl;
    ParameterControl historyRangeLowControl;
    ControlButton loudnessClearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton loudnessResetClickButton { "RESET-CLICK" };
    ControlButton historyClearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton historyResetClickButton { "RESET-CLICK" };
    std::unique_ptr<SubmoduleButtonAttachment> loudnessClearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> loudnessResetClickAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyClearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyResetClickAttachment;
    ControlButton centerSectionsButton { "CENTER-PARTS" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton resetClickButton { "RESET-CLICK" };
    ControlButton peakRmsVisibleButton { "PEAK/RMS" };
    ControlButton loudnessVisibleButton { "LOUDNESS" };
    ControlButton historyVisibleButton { "HISTORY" };
    ControlButton historyMomentaryButton { "M" };
    ControlButton historyShortTermButton { "S" };
    ControlButton historyIntegratedButton { "I" };
    ControlButton historyHorizontalZoomButton { "ZOOM-HORIZ" };
    ControlButton historyVerticalZoomButton { "ZOOM-VERT" };
    ControlButton historyHorizontalReadoutsButton { "RO-HORIZ" };
    ControlButton historyVerticalReadoutsButton { "RO-VERT" };
    std::unique_ptr<SubmoduleButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> resetClickAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> peakRmsVisibleAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> loudnessVisibleAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyVisibleAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyMomentaryAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyShortTermAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyIntegratedAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyHorizontalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyVerticalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyHorizontalReadoutsAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> historyVerticalReadoutsAttachment;
};
