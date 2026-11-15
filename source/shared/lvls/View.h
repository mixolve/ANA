#pragma once

#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "shared/lvls/Processor.h"

#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <utility>
#include <vector>

class PluginProcessor;

class LvlsView final : public juce::Component, private juce::Timer
{
public:
    explicit LvlsView(PluginProcessor& processorRef);

    void refreshPartLayout();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void refreshPersistentViewState();
    void setPersistentParameter(const char* parameterId, float value);
    void layoutPeakModeButtons();
    int getLvlsWidth(bool loudness = false) const noexcept;
    std::array<bool, 3> getVisibleParts() const noexcept;
    std::array<int, 3> getMinimumPartWidths() const noexcept;
    std::array<juce::Rectangle<int>, 3> getPartBounds() const noexcept;
    int findPartSeparator(int x) const noexcept;

    PluginProcessor& processor;
    std::array<float, 4> peakValues { ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels };
    std::array<float, 4> rmsValues { ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels };
    std::array<float, 4> peakMaximumValues { ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels };
    std::array<float, 4> peakHoldValues { ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels };
    std::array<float, 4> rmsMaximumValues { ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels, ana::lvls::LvlsProcessor::minimumDecibels };
    float momentaryLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float shortTermLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float integratedLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float momentaryMaximumLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float shortTermMaximumLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float integratedMaximumLufs = ana::lvls::LvlsProcessor::minimumDecibels;
    float loudnessRange = 0.0f;
    // Numerical readouts hold a snapshot; the meters above remain live.
    ana::lvls::LvlsProcessor::Values readoutPeakValues;
    ana::lvls::LvlsProcessor::Values readoutLoudnessValues;
    ana::lvls::LvlsProcessor::Values readoutMetricsValues;
    std::array<float, 4> pendingPeakReadoutGains {};
    std::array<float, 4> pendingMetricsReadoutGains {};
    std::array<uint64_t, 3> lastReadoutResetEpochs {};
    uint32_t readoutWindowStartMs = 0;
    int lastReadoutIntervalMs = -1;
    bool readoutWindowStarted = false;
    bool readoutWindowHasAudio = false;
    float metricsTruePeak = ana::lvls::LvlsProcessor::minimumDecibels;
    std::array<juce::String, 4> metricsDifferenceReadouts { "---", "---", "---", "---" };
    uint64_t displayedRevision = 0;
    bool displayedOffline = false;
    std::array<float, 3> partWeights { 1.0f, 1.0f, 1.0f };
    std::array<int, 3> dragStartWidths {};
    int draggedPartSeparator = -1;
    int draggedLeftPart = -1;
    int draggedRightPart = -1;
    int hoveredPartSeparator = -1;
    int dragStartX = 0;
    ControlButton peakModeButton { "PEAK" };
    ControlButton rmsModeButton { "RMS" };
    ControlButton peakChannelModeButton { "MS" };
    bool showPeakLvls = true;
    bool showMidSideLvls = false;
};
