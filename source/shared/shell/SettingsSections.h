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

    ParameterControl overallMonitorControl;
    ParameterControl crossoverCountControl;
    ParameterControl styleControl;
    ParameterControl opacityControl;
    ParameterControl slopeControl;
    ParameterControl timeControl;
    ParameterControl timeNoteControl;
    ControlButton timeBaseButton { "N" };
    ControlButton addCrossoverButton { "ADD" };
    ControlButton removeCrossoverButton { "DEL" };
    ControlButton equalHeightButton { "EQUAL-HEIGHT" };
    ControlButton horizontalZoomButton { "ZOOM-HORIZ" };
    ControlButton verticalZoomButton { "ZOOM-VERT" };
    ControlButton horizontalReadoutsButton { "READOUT\nHORIZ" };
    ControlButton verticalReadoutsButton { "READOUT\nVERT" };
    ControlButton monitorControlsButton { "BAND\nMONITOR" };
    ControlButton toolsButton { "TOOLS" };
    ControlButton linkedNormalizeButton { "NORM\nALL" };
    ControlButton leftToRightButton { "L TO R" };
    std::unique_ptr<SubmoduleButtonAttachment> equalHeightAttachment;
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
    explicit SpecSettingsSection(PluginProcessor& processor, bool map = false);
    bool isMap = false;

    // Both FREQ and MAP use the existing seven-choice monitor-mode parameter.
    ParameterControl monitorModeControl;
    ParameterControl fftSizeControl;
    ParameterControl fftOverlapControl;
    ParameterControl mapTimeOverlapControl;
    ParameterControl mapTimeControl;
    ParameterControl mapTimeNoteControl;
    ControlButton mapTimeBaseButton { "N" };
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
    ControlButton highQualityRenderingButton { "HQR" };
    ControlButton mapLeftToRightButton { "L TO R" };
    ControlButton horizontalReadoutsButton { "READOUT\nHORIZ" };
    ControlButton verticalReadoutsButton { "READOUT\nVERT" };
    ControlButton clearOnPlayButton { "CLEAR\nON PLAY" };
    ControlButton resetClickButton { "RESET CLICK" };
    ControlButton cursorButton { "CURSOR\nHORIZ" };
    ControlButton cursorNotesButton { "CURSOR\nNOTE" };
    ControlButton cursorVerticalButton { "CURSOR\nVERT" };
    ControlButton splitViewButton { "SPLIT" };
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
    std::unique_ptr<SubmoduleButtonAttachment> splitViewAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> horizontalZoomAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> verticalZoomAttachment;
};

struct CorrSettingsSection
{
    explicit CorrSettingsSection(PluginProcessor& processor, int mode = 0);
    int submoduleMode = 0;

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
    ControlButton clearOnPlayButton { "CLEAR\nON PLAY" };
    ControlButton resetClickButton { "RESET CLICK" };
    ControlButton horizontalReadoutsButton { "READOUT\nHORIZ" };
    ControlButton verticalReadoutsButton { "READOUT\nVERT" };
    ControlButton cursorButton { "CURSOR\nHORIZ" };
    ControlButton cursorNotesButton { "CURSOR\nNOTE" };
    ControlButton cursorVerticalButton { "CURSOR\nVERT" };
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

    ParameterControl peakRangeHighControl;
    ParameterControl peakRangeLowControl;
    ParameterControl rmsWindowControl;
    ParameterControl peakHoldControl;
    ParameterControl readoutIntervalControl;
    ParameterControl loudnessRangeHighControl;
    ParameterControl loudnessRangeLowControl;
    ControlButton clearOnPlayButton { "CLEAR\nON PLAY" };
    ControlButton resetClickButton { "RESET CLICK" };
    ControlButton peakRmsVisibleButton { "PEAK/RMS" };
    ControlButton loudnessVisibleButton { "LOUDNESS" };
    ControlButton metricsVisibleButton { "OTHERS" };
    std::unique_ptr<SubmoduleButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> resetClickAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> peakRmsVisibleAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> loudnessVisibleAttachment;
    std::unique_ptr<SubmoduleButtonAttachment> metricsVisibleAttachment;

};
