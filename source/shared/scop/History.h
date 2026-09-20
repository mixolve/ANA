#pragma once

#include "shared/scop/AnalysisTypes.h"
#include "shared/scop/Crossover.h"
#include "shared/scop/Settings.h"

#include <cstddef>
#include <array>
#include <cstdint>

namespace ana
{
class ScopHistory
{
public:
    using AnalysisChannelSamples = scop::AnalysisChannelSamples;

    void reset(size_t columnCount,
               double timeMilliseconds,
               size_t activeBandCount,
               uint64_t writeCursor,
               const std::array<ScopChannelMode, dsp::LinkwitzRileyCrossover::numBands>& modes);
    void resize(size_t columnCount);
    void clearBand(size_t bandIndex);
    void clearWideband();
    void clearRecordedData();
    void resetColumnAccumulator();
    void accumulateBand(size_t bandIndex, const AnalysisChannelSamples& samples) noexcept;
    void accumulateWideband(const AnalysisChannelSamples& samples) noexcept;
    void appendColumn(size_t activeBandCount);
    void writeColumn(size_t activeBandCount, size_t columnIndex);

    std::array<ScopChannelMode, dsp::LinkwitzRileyCrossover::numBands> channelModes {};
    std::array<scop::AnalysisChannelEnvelopes, dsp::LinkwitzRileyCrossover::numBands> bands;
    scop::AnalysisChannelEnvelopes wideband;
    std::array<AnalysisChannelSamples, dsp::LinkwitzRileyCrossover::numBands> columnMinimums;
    std::array<AnalysisChannelSamples, dsp::LinkwitzRileyCrossover::numBands> columnMaximums;
    AnalysisChannelSamples widebandColumnMinimums;
    AnalysisChannelSamples widebandColumnMaximums;
    uint64_t readCursor = 0;
    double columnSampleProgress = 0.0;
    double timeMilliseconds = 0.0;
    size_t bandCount = 0;
    bool containsRecordedData = false;
};
}
