#pragma once

#include "shared/scop/BandView.h"
#include "offline/shell/AnalysisResult.h"
#include "shared/scop/AnalysisTypes.h"
#include "shared/scop/Crossover.h"
#include "shared/scop/Settings.h"

#include <cstddef>
#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>

class OfflineScopView final : public ana::scop::BandView, private juce::Timer
{
public:
    explicit OfflineScopView(PluginProcessor& processorRef);

    void refreshWaveform();
    void refreshDisplaySettings();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void clearBandHistory(size_t bandIndex) override;
    void createBandExtras(size_t bandIndex) override;
    void refreshBandExtras(
        size_t bandIndex, juce::Rectangle<int> laneBounds, bool isVisibleBand,
        bool zoomAvailable, bool showVerticalZoomSlider,
        int topControlsBottom) override;
    bool isBandNormalizationEnabled(size_t bandIndex) const override;
    int minimumBandHeight() const noexcept override;
    const ana::scop::Envelope* getBandEnvelope(
        size_t bandIndex, size_t channelIndex) const noexcept override;
    void renderOfflineAnalysis(std::shared_ptr<const ana::offline::AnalysisResult> analysis);

    std::array<std::unique_ptr<EllipsisLabel>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandTimeStartLabels;
    std::array<std::unique_ptr<EllipsisLabel>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandTimeEndLabels;
    std::array<std::unique_ptr<RangeSlider>,
               ana::dsp::LinkwitzRileyCrossover::numBands> bandRangeSliders;
    std::array<ana::ScopChannelMode, ana::dsp::LinkwitzRileyCrossover::numBands> displayedChannelModes {};
    size_t displayedBandCount = 0;
    std::array<bool, ana::dsp::LinkwitzRileyCrossover::numBands> clearedBands {};
    bool widebandCleared = false;
    std::shared_ptr<const ana::offline::AnalysisResult> displayedOfflineAnalysis;
    size_t offlineAnalysisColumnCount = 0;
    uint64_t offlineAnalysisRevision = 0;
};
