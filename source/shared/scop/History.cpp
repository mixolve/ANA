#include "History.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ana
{
void ScopHistory::reset(const size_t columnCount,
                         const double newTimeMilliseconds,
                         const size_t activeBandCount,
                         const uint64_t writeCursor,
                         const std::array<ScopChannelMode, dsp::LinkwitzRileyCrossover::numBands>& modes)
{
    timeMilliseconds = newTimeMilliseconds;
    bandCount = activeBandCount;
    containsRecordedData = false;
    channelModes = modes;
    readCursor = writeCursor;
    columnSampleProgress = 0.0;
    resetColumnAccumulator();

    const auto count = std::max<size_t>(1, columnCount);
    const auto initialise = [count] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            envelope.minimums.assign(count, std::numeric_limits<float>::max());
            envelope.maximums.assign(count, std::numeric_limits<float>::lowest());
        }
    };

    for (auto& band : bands)
        initialise(band);
    initialise(wideband);
}

void ScopHistory::resize(const size_t columnCount)
{
    const auto targetColumnCount = std::max<size_t>(1, columnCount);
    const auto resizeEnvelopes = [targetColumnCount] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            const auto oldMinimums = std::move(envelope.minimums);
            const auto oldMaximums = std::move(envelope.maximums);
            envelope.minimums.assign(targetColumnCount, std::numeric_limits<float>::max());
            envelope.maximums.assign(targetColumnCount, std::numeric_limits<float>::lowest());

            if (oldMinimums.empty() || oldMaximums.size() != oldMinimums.size())
                continue;

            const auto sourceColumnCount = oldMinimums.size();
            for (size_t targetColumn = 0; targetColumn < targetColumnCount; ++targetColumn)
            {
                const auto firstSourceColumn = std::min(sourceColumnCount - 1,
                    targetColumn * sourceColumnCount / targetColumnCount);
                const auto endSourceColumn = std::max(firstSourceColumn + 1,
                    std::min(sourceColumnCount,
                        static_cast<size_t>(std::ceil(
                            static_cast<double>(targetColumn + 1)
                            * static_cast<double>(sourceColumnCount)
                            / static_cast<double>(targetColumnCount)))));

                for (auto sourceColumn = firstSourceColumn;
                     sourceColumn < endSourceColumn;
                     ++sourceColumn)
                {
                    if (oldMinimums[sourceColumn] <= oldMaximums[sourceColumn])
                    {
                        envelope.minimums[targetColumn] = std::min(
                            envelope.minimums[targetColumn], oldMinimums[sourceColumn]);
                        envelope.maximums[targetColumn] = std::max(
                            envelope.maximums[targetColumn], oldMaximums[sourceColumn]);
                    }
                }
            }
        }
    };

    for (auto& band : bands)
        resizeEnvelopes(band);
    resizeEnvelopes(wideband);
}

void ScopHistory::clearBand(const size_t bandIndex)
{
    if (bandIndex >= bands.size())
        return;

    for (auto& envelope : bands[bandIndex])
    {
        envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
        envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
    }
    columnMinimums[bandIndex].fill(std::numeric_limits<float>::max());
    columnMaximums[bandIndex].fill(std::numeric_limits<float>::lowest());
}

void ScopHistory::clearWideband()
{
    for (auto& envelope : wideband)
    {
        envelope.minimums.assign(envelope.minimums.size(), std::numeric_limits<float>::max());
        envelope.maximums.assign(envelope.maximums.size(), std::numeric_limits<float>::lowest());
    }
    widebandColumnMinimums.fill(std::numeric_limits<float>::max());
    widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
}

void ScopHistory::clearRecordedData()
{
    const auto clearEnvelopes = [] (auto& envelopes)
    {
        for (auto& envelope : envelopes)
        {
            std::fill(envelope.minimums.begin(), envelope.minimums.end(),
                      std::numeric_limits<float>::max());
            std::fill(envelope.maximums.begin(), envelope.maximums.end(),
                      std::numeric_limits<float>::lowest());
        }
    };

    for (auto& band : bands)
        clearEnvelopes(band);
    clearEnvelopes(wideband);
    containsRecordedData = false;
}

void ScopHistory::resetColumnAccumulator()
{
    for (auto& band : columnMinimums)
        band.fill(std::numeric_limits<float>::max());
    for (auto& band : columnMaximums)
        band.fill(std::numeric_limits<float>::lowest());
    widebandColumnMinimums.fill(std::numeric_limits<float>::max());
    widebandColumnMaximums.fill(std::numeric_limits<float>::lowest());
}

void ScopHistory::accumulateBand(const size_t bandIndex, const AnalysisChannelSamples& samples) noexcept
{
    if (bandIndex >= columnMinimums.size())
        return;

    for (size_t modeIndex = 0; modeIndex < samples.size(); ++modeIndex)
    {
        columnMinimums[bandIndex][modeIndex] = std::min(
            columnMinimums[bandIndex][modeIndex], samples[modeIndex]);
        columnMaximums[bandIndex][modeIndex] = std::max(
            columnMaximums[bandIndex][modeIndex], samples[modeIndex]);
    }
}

void ScopHistory::accumulateWideband(const AnalysisChannelSamples& samples) noexcept
{
    for (size_t modeIndex = 0; modeIndex < samples.size(); ++modeIndex)
    {
        widebandColumnMinimums[modeIndex] = std::min(
            widebandColumnMinimums[modeIndex], samples[modeIndex]);
        widebandColumnMaximums[modeIndex] = std::max(
            widebandColumnMaximums[modeIndex], samples[modeIndex]);
    }
}

void ScopHistory::appendColumn(const size_t activeBandCount)
{
    if (bands.front().front().minimums.empty())
        return;

    const auto count = std::min(activeBandCount, bands.size());
    for (size_t bandIndex = 0; bandIndex < count; ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bands[bandIndex].size(); ++modeIndex)
        {
            auto& envelope = bands[bandIndex][modeIndex];
            if (envelope.minimums.size() > 1)
            {
                std::move(envelope.minimums.begin() + 1, envelope.minimums.end(), envelope.minimums.begin());
                std::move(envelope.maximums.begin() + 1, envelope.maximums.end(), envelope.maximums.begin());
            }
            envelope.minimums.back() = columnMinimums[bandIndex][modeIndex];
            envelope.maximums.back() = columnMaximums[bandIndex][modeIndex];
        }
    }

    for (size_t modeIndex = 0; modeIndex < wideband.size(); ++modeIndex)
    {
        auto& envelope = wideband[modeIndex];
        if (envelope.minimums.size() > 1)
        {
            std::move(envelope.minimums.begin() + 1, envelope.minimums.end(), envelope.minimums.begin());
            std::move(envelope.maximums.begin() + 1, envelope.maximums.end(), envelope.maximums.begin());
        }
        envelope.minimums.back() = widebandColumnMinimums[modeIndex];
        envelope.maximums.back() = widebandColumnMaximums[modeIndex];
    }

    containsRecordedData = true;
}

void ScopHistory::writeColumn(const size_t activeBandCount, const size_t columnIndex)
{
    if (bands.front().front().minimums.empty()
        || columnIndex >= bands.front().front().minimums.size())
        return;

    const auto count = std::min(activeBandCount, bands.size());
    for (size_t bandIndex = 0; bandIndex < count; ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bands[bandIndex].size(); ++modeIndex)
        {
            auto& envelope = bands[bandIndex][modeIndex];
            envelope.minimums[columnIndex] = columnMinimums[bandIndex][modeIndex];
            envelope.maximums[columnIndex] = columnMaximums[bandIndex][modeIndex];
        }
    }

    for (size_t modeIndex = 0; modeIndex < wideband.size(); ++modeIndex)
    {
        wideband[modeIndex].minimums[columnIndex] = widebandColumnMinimums[modeIndex];
        wideband[modeIndex].maximums[columnIndex] = widebandColumnMaximums[modeIndex];
    }

    containsRecordedData = true;
}
}
