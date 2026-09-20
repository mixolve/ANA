#pragma once

#include "shared/shell/Controls.h"
#include "shared/scop/AnalysisTypes.h"
#include "Processor.h"
#include "shared/scop/History.h"

#include <cstddef>
#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>

class PluginProcessor;

class ScopView final : public juce::Component, private juce::Timer
{
public:
    explicit ScopView(PluginProcessor& processorRef);

    void setFrozen(bool shouldFreeze);
    void clearHistory();
    void refreshWaveform();
    void equalizeBandHeights();
    void refreshDisplaySettings();
    void setFullSourceView(bool shouldShowFullSource);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void resetHistory();
    void resizeHistory(size_t newColumnCount);
    void clearBandHistory(size_t bandIndex);
    void appendHistoryColumn(size_t activeBandCount);
    void refreshBandModeButtons();
    ana::scop::AnalysisChannelSamples getRtmAnalysisChannelSamples(
        size_t bandIndex, size_t sampleIndex) const noexcept;
    ana::scop::AnalysisChannelSamples getRtmWidebandAnalysisChannelSamples(
        size_t sampleIndex) const noexcept;
    juce::Rectangle<float> getBandBounds(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool shouldShowZoomSliders(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool hasVisibleZoomSliders() const noexcept;
    int findBandSeparator(int y, size_t activeBandCount) const noexcept;
    size_t getWaveformColumnCount() const noexcept;

    PluginProcessor& processor;
    ana::MultibandScop::SampleBatch incomingSamples;
    SliderLookAndFeel bandZoomLookAndFeel;
    std::array<std::array<std::unique_ptr<ControlButton>, 6>, ana::MultibandScop::numBands> bandModeButtons;
    std::array<std::unique_ptr<ControlButton>, ana::MultibandScop::numBands> bandClearButtons;
    std::array<std::unique_ptr<ControlButton>, ana::MultibandScop::numBands> bandSingleViewButtons;
    std::array<std::unique_ptr<juce::Slider>, ana::MultibandScop::numBands> bandZoomSliders;
    std::array<std::unique_ptr<EllipsisLabel>, ana::MultibandScop::numBands> bandZoomValueLabels;
    ana::ScopHistory history;
    std::array<float, ana::MultibandScop::numBands> bandHeightWeights;
    double rtmResumeTimeMilliseconds = 0.0;
    size_t leftToRightColumn = 0;
    int renderedLeftToRight = -1;
    bool frozen = false;
    bool updatingBandControls = false;
    bool fullSourceView = false;
    int lastComponentWidth = 0;
    int lastComponentHeight = 0;
    int singleViewBand = -1;
    int draggedBandSeparator = -1;
};
