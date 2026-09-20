#pragma once

#include "shared/shell/Controls.h"
#include "../shell/AraAnalysisResult.h"
#include "shared/scop/AnalysisTypes.h"
#include "shared/scop/Crossover.h"
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
    std::function<void(const juce::String&)> onAraUpdateStatus;

private:
    void timerCallback() override;
    void resetHistory();
    void resizeHistory(size_t newColumnCount);
    void clearBandHistory(size_t bandIndex);
    void renderAraAnalysis(std::shared_ptr<const ana::ara::AnalysisResult> analysis);
    void refreshBandModeButtons();
    void normalizeBandWithZoom(size_t bandIndex);
    juce::Rectangle<float> getBandBounds(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool shouldShowZoomSliders(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool hasVisibleZoomSliders() const noexcept;
    int findBandSeparator(int y, size_t activeBandCount) const noexcept;
    size_t getWaveformColumnCount() const noexcept;

    PluginProcessor& processor;
    SliderLookAndFeel bandZoomLookAndFeel;
    std::array<std::array<std::unique_ptr<ControlButton>, 6>, ana::dsp::LinkwitzRileyCrossover::numBands> bandModeButtons;
    std::array<std::unique_ptr<ControlButton>, ana::dsp::LinkwitzRileyCrossover::numBands> bandClearButtons;
    std::array<std::unique_ptr<ControlButton>, ana::dsp::LinkwitzRileyCrossover::numBands> bandSingleViewButtons;
    std::array<std::unique_ptr<juce::Slider>, ana::dsp::LinkwitzRileyCrossover::numBands> bandZoomSliders;
    std::array<std::unique_ptr<EllipsisLabel>, ana::dsp::LinkwitzRileyCrossover::numBands> bandZoomValueLabels;
    std::array<std::unique_ptr<ControlButton>, ana::dsp::LinkwitzRileyCrossover::numBands> bandNormalizeButtons;
    std::array<std::unique_ptr<RangeSlider>, ana::dsp::LinkwitzRileyCrossover::numBands> bandRangeSliders;
    ana::ScopHistory history;
    std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands> bandHeightWeights;
    std::array<bool, ana::dsp::LinkwitzRileyCrossover::numBands> clearedBands {};
    bool widebandCleared = false;
    std::shared_ptr<const ana::ara::AnalysisResult> displayedAraAnalysis;
    size_t araAnalysisColumnCount = 0;
    uint64_t araAnalysisRevision = 0;
    bool showingAraAnalysis = false;
    bool frozen = false;
    bool updatingBandControls = false;
    bool fullSourceView = false;
    int lastComponentWidth = 0;
    int lastComponentHeight = 0;
    int singleViewBand = -1;
    int draggedBandSeparator = -1;
};
