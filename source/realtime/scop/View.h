#pragma once

#include "shared/scop/BandView.h"
#include "shared/scop/AnalysisTypes.h"
#include "Processor.h"
#include "shared/scop/History.h"

#include <cstddef>
#include <cstdint>
#include <JuceHeader.h>

#include <array>
#include <functional>
#include <memory>

class RealtimeScopView final : public ana::scop::BandView, private juce::Timer
{
public:
    explicit RealtimeScopView(PluginProcessor& processorRef);

    void setFrozen(bool shouldFreeze);
    void setFullSourceView(bool shouldShowFullSource);
    void clearHistory();
    void refreshDisplaySettings();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    uint64_t clearRevision=0;
    void timerCallback() override;
    void resetHistory();
    void resizeHistory(size_t newColumnCount);
    void clearBandHistory(size_t bandIndex) override;
    bool isBandNormalizationEnabled(size_t bandIndex) const override;
    int minimumBandHeight() const noexcept override;
    const ana::scop::Envelope* getBandEnvelope(
        size_t bandIndex, size_t channelIndex) const noexcept override;
    void appendHistoryColumn(size_t activeBandCount);
    ana::scop::AnalysisChannelSamples getRealtimeAnalysisChannelSamples(
        size_t bandIndex, size_t sampleIndex) const noexcept;

    ana::MultibandScop::SampleBatch incomingSamples;
    ana::ScopHistory history;
    uint64_t observedResetRevision = 0;
    uint64_t observedSlopeRevision = 0;
    size_t leftToRightColumn = 0;
    int renderedLeftToRight = -1;
    bool frozen = false;
    bool suspendedWhileHidden = false;
    bool usingPeakAggregation = false;
    int lastComponentWidth = 0;
    int lastComponentHeight = 0;
};
