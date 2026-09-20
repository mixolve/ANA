#pragma once

#include "Controls.h"
#include "ParameterControl.h"
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
    ControlButton zoomControlsButton { "ZOOM" };
    ControlButton monitorControlsButton { "MONITOR" };
    ControlButton toolsButton { "TOOLS" };
    ControlButton leftToRightButton { "LEFT-TO-RIGHT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zoomControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toolsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> leftToRightAttachment;
    std::array<std::unique_ptr<ParameterControl>, ana::dsp::LinkwitzRileyCrossover::numCrossovers> crossoverControls;
};

struct SpecSettingsSection
{
    explicit SpecSettingsSection(PluginProcessor& processor);

    ParameterControl fftSizeControl;
    ParameterControl fftOverlapControl;
    ParameterControl mapTimeOverlapControl;
    ParameterControl averageTimeControl;
    ParameterControl smoothingControl;
    ParameterControl frequencyScaleControl;
    ParameterControl firstGraphTypeControl;
    ParameterControl secondGraphTypeControl;
    ParameterControl slopeControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    ControlButton filledDisplayButton { "FILLED-DISPLAY" };
    ControlButton secondGraphButton { "2ND-GRAPH" };
    ControlButton antiAliasButton { "ANTI-ALIAS" };
    ControlButton highQualityRenderingButton { "HIGH-QUALITY RENDERING" };
    ControlButton mapLeftToRightButton { "LEFT-TO-RIGHT" };
    ControlButton rangesButton { "RANGES" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton cursorButton { "CURSOR" };
    ControlButton monitorControlsButton { "MONITOR" };
    ControlButton zoomControlsButton { "ZOOM" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filledDisplayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> secondGraphAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> antiAliasAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> highQualityRenderingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mapLeftToRightAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rangesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cursorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zoomControlsAttachment;
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
    ParameterControl secondGraphTypeControl;
    ControlButton filledDisplayButton { "FILLED-DISPLAY" };
    ControlButton secondGraphButton { "2ND-GRAPH" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton rangesButton { "RANGES" };
    ControlButton cursorButton { "CURSOR" };
    ControlButton zoomControlsButton { "ZOOM" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filledDisplayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> secondGraphAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rangesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cursorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zoomControlsAttachment;
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
    ControlButton centerSectionsButton { "CENTER-PARTS" };
    ControlButton clearOnPlayButton { "CLEAR-ON-PLAY" };
    ControlButton peakRmsVisibleButton { "PEAK/RMS" };
    ControlButton loudnessVisibleButton { "LOUDNESS" };
    ControlButton historyVisibleButton { "HISTORY" };
    ControlButton historyMomentaryButton { "M" };
    ControlButton historyShortTermButton { "S" };
    ControlButton historyIntegratedButton { "I" };
    ControlButton historyZoomButton { "ZOOM" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clearOnPlayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> peakRmsVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loudnessVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> historyVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> historyMomentaryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> historyShortTermAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> historyIntegratedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> historyZoomAttachment;
};
