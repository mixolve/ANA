#include "View.h"
#include "shell/Processor.h"
#include "shared/spec/Display.h"
#include "shared/spec/MapFrequencyScale.h"
#include "shared/spec/Settings.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/GraphColours.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace ana::ui::analyzer_detail;
using namespace ana::spec::display;

void OfflineSpecView::rebuildOfflineSpectrogramImage()
{
    if (viewMode != ViewMode::map)
        return;

    // Preserve the last completed raster across REAPER editor-peer recreation.
    if (! offlineSpectrogramImage.isValid())
        offlineSpectrogramImage = processor.getCachedOfflineSpectrogramImage();

    const auto analysisResult = processor.getAnalysisResult();

    if (analysisResult != nullptr && analysisResult->selectionEmpty)
    {
        const auto bounds = getPlotBounds().toNearestInt();
        offlineSpectrogramImage = juce::Image(juce::Image::ARGB,
            std::max(1, bounds.getWidth()), std::max(1, bounds.getHeight()), true);
        offlineSpectrogramImage.clear(offlineSpectrogramImage.getBounds(), ana::ui::background);
        processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
        const auto mode = readSpecMonitorMode(processor);
        renderedOfflineMonitorMode = ana::spec::monitorModeIndex(mode);
        renderedOfflineSplitView = ana::spec::supportsSplitView(mode);
        repaint();
        return;
    }

    // Host/ARA churn must not replace the last valid raster with an empty intermediate result.
    if (analysisResult == nullptr || analysisResult->specMap == nullptr || ! analysisResult->specMap->isValid())
        return;

    const auto bounds = getPlotBounds().toNearestInt();
    const auto width = std::max(1, bounds.getWidth());
    const auto height = std::max(1, bounds.getHeight());
    // Commit a replacement raster only after it is fully rendered.
    juce::Image nextSpectrogramImage(juce::Image::ARGB, width, height, true);
    nextSpectrogramImage.clear(
        nextSpectrogramImage.getBounds(),
        ana::ui::background);

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specMapHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);
    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = highQuality ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);

    const auto& map = *analysisResult->specMap;
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto firstIndex = ana::spec::channelIndex(firstChannel);
    const auto secondIndex = ana::spec::channelIndex(secondChannel);
    const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
    const auto useSecondSpectrum = secondChannel != firstChannel && ! useSplitView;
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto colourForValue = [&] (const float value, const float frequency)
    {
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto displayValue = value + slopeOffset;
        return spectrogramColour(juce::jlimit(0.0f, 1.0f,
            (displayValue - lowRange) / (highRange - lowRange)), colourMap);
    };

    const auto mapAt = [&map] (const size_t channel, const size_t row, const size_t column)
    {
        return map.levels[channel][row * map.columnCount + column];
    };

    // Use the native raster only for an exact geometry/range match.
    const auto paneHeight = useSplitView ? std::max(1, height / 2) : height;
    const auto allColumnsPopulated = std::all_of(
        map.columnFrameCounts.begin(), map.columnFrameCounts.end(),
        [] (const uint32_t count) { return count != 0; });
    const auto nativeRaster = allColumnsPopulated
        && map.timeSpans.size() == 1
        && map.timeSpans.front().start <= 0.0 && map.timeSpans.front().end >= 1.0
        && firstIndex != 0 && secondIndex != 0
        && map.columnCount == static_cast<size_t>(width)
        && map.rowCount == static_cast<size_t>(paneHeight)
        && (! useSplitView || height % 2 == 0)
        && juce::approximatelyEqual(mapTimeRangeStart, 0.0f)
        && juce::approximatelyEqual(mapTimeRangeEnd, 1.0f)
        && frequencyScale == ana::frequency_scale::Scale::logarithmic
        && juce::approximatelyEqual(lowFrequency, ana::frequency_scale::minimumHz)
        && juce::approximatelyEqual(highFrequency, ana::frequency_scale::maximumHz);

    // HQ always uses max-bilinear sampling for both split panes; native blits are non-HQ only.
    if (nativeRaster && ! highQuality)
    {
        juce::Image::BitmapData pixels(nextSpectrogramImage, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
        {
            const auto secondHalf = useSplitView && y >= paneHeight;
            const auto localY = secondHalf ? y - paneHeight : y;
            const auto row = static_cast<size_t>(juce::jlimit(0, paneHeight - 1, localY));
            const auto channel = secondHalf ? secondIndex : firstIndex;
            const auto centreNormalised = 1.0f
                - (static_cast<float>(localY) + 0.5f) / static_cast<float>(paneHeight);
            const auto centreFrequency = ana::frequency_scale::frequencyAt(
                frequencyScale, lowFrequency, highFrequency, centreNormalised);
            if (centreFrequency > static_cast<float>(map.sampleRate * 0.5))
                continue;
            for (int x = 0; x < width; ++x)
            {
                auto value = mapAt(channel, row, static_cast<size_t>(x));
                if (! useSplitView && useSecondSpectrum)
                    value = std::max(value,
                        mapAt(secondIndex, row, static_cast<size_t>(x)));
                pixels.setPixelColour(x, y, colourForValue(value, centreFrequency));
            }
        }

        offlineSpectrogramImage = std::move(nextSpectrogramImage);
        processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
        renderedOfflineMonitorMode = ana::spec::monitorModeIndex(monitorMode);
        renderedOfflineSplitView = useSplitView;
        repaint();
        return;
    }

    struct FrequencyLookup
    {
        size_t row0 = 0;
        size_t row1 = 0;
        float rowMix = 0.0f;
        size_t rowPeakFirst = 0;
        size_t rowPeakLast = 0;
        bool rowUsePeak = false;
        size_t fftBin0 = 0;
        size_t fftBin1 = 0;
        float fftBinMix = 0.0f;
        size_t fftPeakFirst = 0;
        size_t fftPeakLast = 0;
        bool fftUsePeak = false;
        float centreFrequency = 0.0f;
    };
    struct TimeLookup
    {
        size_t column0 = 0;
        size_t column1 = 0;
        float mix = 0.0f;
        size_t peakFirst = 0;
        size_t peakLast = 0;
        size_t spanFirst = 0;
        size_t spanLast = 0;
        bool usePeak = false;
        bool valid = false;
    };

    std::vector<FrequencyLookup> frequencyForY(static_cast<size_t>(height));
    std::vector<bool> useSecondChannelForY(static_cast<size_t>(height), false);

    const auto sourceRowForFrequency = [&map] (const float frequency)
    {
        const auto clampedFrequency = juce::jlimit(
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz,
            frequency);
        const auto normalised = ana::map_frequency_scale::normalisedForFrequency(
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz,
            clampedFrequency);
        return juce::jlimit(0.0f, static_cast<float>(map.rowCount - 1),
            (1.0f - normalised) * static_cast<float>(map.rowCount) - 0.5f);
    };

    const auto makeFrequencyLookup = [&] (const int localY, const int localHeight)
    {
        FrequencyLookup lookup;
        const auto safeHeight = std::max(1, localHeight);
        const auto invHeight = 1.0f / static_cast<float>(safeHeight);
        const auto y = static_cast<float>(juce::jlimit(0, safeHeight - 1, localY));
        const auto topNormalised = 1.0f - y * invHeight;
        const auto bottomNormalised = 1.0f - (y + 1.0f) * invHeight;
        const auto centreNormalised = 1.0f - (y + 0.5f) * invHeight;
        const auto centreFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, centreNormalised);
        const auto upperFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, topNormalised);
        const auto lowerFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, bottomNormalised);
        lookup.centreFrequency = centreFrequency;

        const auto exactRow = sourceRowForFrequency(centreFrequency);
        lookup.row0 = static_cast<size_t>(std::floor(exactRow));
        lookup.row1 = std::min(map.rowCount - 1, lookup.row0 + 1);
        lookup.rowMix = exactRow - static_cast<float>(lookup.row0);

        const auto upperRow = sourceRowForFrequency(upperFrequency);
        const auto lowerRow = sourceRowForFrequency(lowerFrequency);
        const auto firstRowCoord = std::min(upperRow, lowerRow);
        const auto lastRowCoord = std::max(upperRow, lowerRow);
        if (lastRowCoord - firstRowCoord > 1.0f)
        {
            const auto first = std::max(0, static_cast<int>(std::ceil(firstRowCoord)));
            const auto last = std::min(static_cast<int>(map.rowCount - 1),
                                       static_cast<int>(std::floor(lastRowCoord)));
            if (first <= last)
            {
                lookup.rowPeakFirst = static_cast<size_t>(first);
                lookup.rowPeakLast = static_cast<size_t>(last);
                lookup.rowUsePeak = true;
            }
        }

        const auto binFrequency = map.fftSize > 0
            ? static_cast<float>(map.sampleRate) / static_cast<float>(map.fftSize)
            : 0.0f;
        if (map.binCount >= 2 && binFrequency > 0.0f)
        {
            const auto clampBin = [&] (const float frequency)
            {
                return juce::jlimit(1.0f, static_cast<float>(map.binCount - 1),
                                    frequency / binFrequency);
            };
            const auto exactBin = clampBin(centreFrequency);
            lookup.fftBin0 = static_cast<size_t>(std::floor(exactBin));
            lookup.fftBin1 = std::min(map.binCount - 1, lookup.fftBin0 + 1);
            lookup.fftBinMix = exactBin - static_cast<float>(lookup.fftBin0);

            const auto upperBin = clampBin(upperFrequency);
            const auto lowerBin = clampBin(lowerFrequency);
            const auto firstBinCoord = std::min(upperBin, lowerBin);
            const auto lastBinCoord = std::max(upperBin, lowerBin);
            if (lastBinCoord - firstBinCoord > 1.0f)
            {
                const auto first = std::max(1, static_cast<int>(std::ceil(firstBinCoord)));
                const auto last = std::min(static_cast<int>(map.binCount - 1),
                                           static_cast<int>(std::floor(lastBinCoord)));
                if (first <= last)
                {
                    lookup.fftPeakFirst = static_cast<size_t>(first);
                    lookup.fftPeakLast = static_cast<size_t>(last);
                    lookup.fftUsePeak = true;
                }
            }
        }

        return lookup;
    };

    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto yi = static_cast<size_t>(y);
        frequencyForY[yi] = makeFrequencyLookup(localY, localHeight);
        useSecondChannelForY[yi] = secondHalf;
    }

    std::vector<TimeLookup> timeForX(static_cast<size_t>(width));
    const ana::offline::SpectrogramTimeline timeline(map.columnFrameCounts, map.timeSpans);

    const auto framePositionForTime = [&map] (const float normalisedTime)
    {
        // Cached columns are pixel-centred at (i + 0.5) / N.
        return juce::jlimit(0.0f, static_cast<float>(map.columnCount - 1),
            juce::jlimit(0.0f, 1.0f, normalisedTime)
                * static_cast<float>(map.columnCount) - 0.5f);
    };

    const auto findTimeBracket = [&timeline] (const float normalisedTime)
    {
        TimeLookup lookup;
        const auto bracket = timeline.bracket(normalisedTime);
        lookup.column0 = bracket.column0;
        lookup.column1 = bracket.column1;
        lookup.mix = bracket.mix;
        lookup.spanFirst = bracket.spanFirst;
        lookup.spanLast = bracket.spanLast;
        lookup.valid = bracket.valid;
        return lookup;
    };

    for (int x = 0; x < width; ++x)
    {
        // Max-bilinear downsampling preserves the strongest measured cell in each destination pixel.
        const auto invWidth = 1.0f / static_cast<float>(std::max(1, width));
        const auto centreLocal = (static_cast<float>(x) + 0.5f) * invWidth;
        const auto leftLocal = static_cast<float>(x) * invWidth;
        const auto rightLocal = static_cast<float>(x + 1) * invWidth;
        const auto toMapTime = [&] (const float local)
        {
            return juce::jlimit(0.0f, 1.0f,
                mapTimeRangeStart + (mapTimeRangeEnd - mapTimeRangeStart) * local);
        };

        auto lookup = findTimeBracket(toMapTime(centreLocal));
        const auto firstPosition = framePositionForTime(toMapTime(leftLocal));
        const auto lastPosition = framePositionForTime(toMapTime(rightLocal));
        const auto firstCoord = std::min(firstPosition, lastPosition);
        const auto lastCoord = std::max(firstPosition, lastPosition);
        if (lastCoord - firstCoord > 1.0f)
        {
            const auto first = std::max(static_cast<int>(lookup.spanFirst),
                                        static_cast<int>(std::ceil(firstCoord)));
            const auto last = std::min(static_cast<int>(lookup.spanLast),
                                       static_cast<int>(std::floor(lastCoord)));
            if (first <= last)
            {
                lookup.peakFirst = static_cast<size_t>(first);
                lookup.peakLast = static_cast<size_t>(last);
                lookup.usePeak = true;
            }
        }
        timeForX[static_cast<size_t>(x)] = lookup;
    }

    const auto atRow = [&map] (const size_t channel, const size_t row, const size_t column)
    {
        return map.levels[channel][row * map.columnCount + column];
    };
    const auto atStereoBin = [&map] (const size_t bin, const size_t column)
    {
        return map.stereoDecibels[column * map.binCount + bin];
    };
    const auto sampleHighQuality = [&] (const size_t channel,
                                         const FrequencyLookup& frequency,
                                         const TimeLookup& time)
    {
        // Interpolate in dB space and retain the local maximum when downsampling.
        if (channel == 0 && map.binCount >= 2
            && map.stereoDecibels.size() == map.columnCount * map.binCount)
        {
            const auto first0 = atStereoBin(frequency.fftBin0, time.column0);
            const auto first1 = atStereoBin(frequency.fftBin1, time.column0);
            const auto second0 = atStereoBin(frequency.fftBin0, time.column1);
            const auto second1 = atStereoBin(frequency.fftBin1, time.column1);
            const auto first = first0
                + frequency.fftBinMix * (first1 - first0);
            const auto second = second0
                + frequency.fftBinMix * (second1 - second0);
            auto value = first + time.mix * (second - first);

            if (highQuality && (frequency.fftUsePeak || time.usePeak))
            {
                const auto firstBin = frequency.fftUsePeak
                    ? frequency.fftPeakFirst : frequency.fftBin0;
                const auto lastBin = frequency.fftUsePeak
                    ? frequency.fftPeakLast : frequency.fftBin1;
                const auto firstColumn = time.usePeak ? time.peakFirst : time.column0;
                const auto lastColumn = time.usePeak ? time.peakLast : time.column1;
                auto peak = ana::spec::SpecProcessor::minimumDecibels;
                for (size_t bin = firstBin; bin <= lastBin; ++bin)
                    for (size_t column = firstColumn; column <= lastColumn; ++column)
                        if (map.columnFrameCounts[column] != 0)
                            peak = std::max(peak, atStereoBin(bin, column));
                value = std::max(value, peak);
            }
            return value;
        }

        const auto v00 = atRow(channel, frequency.row0, time.column0);
        const auto v01 = atRow(channel, frequency.row0, time.column1);
        const auto v10 = atRow(channel, frequency.row1, time.column0);
        const auto v11 = atRow(channel, frequency.row1, time.column1);
        const auto v0 = v00 + time.mix * (v01 - v00);
        const auto v1 = v10 + time.mix * (v11 - v10);
        auto value = v0 + frequency.rowMix * (v1 - v0);

        if (highQuality && (frequency.rowUsePeak || time.usePeak))
        {
            const auto firstRow = frequency.rowUsePeak
                ? frequency.rowPeakFirst : frequency.row0;
            const auto lastRow = frequency.rowUsePeak
                ? frequency.rowPeakLast : frequency.row1;
            const auto firstColumn = time.usePeak ? time.peakFirst : time.column0;
            const auto lastColumn = time.usePeak ? time.peakLast : time.column1;
            auto peak = ana::spec::SpecProcessor::minimumDecibels;
            for (size_t row = firstRow; row <= lastRow; ++row)
                for (size_t column = firstColumn; column <= lastColumn; ++column)
                    if (map.columnFrameCounts[column] != 0)
                        peak = std::max(peak, atRow(channel, row, column));
            value = std::max(value, peak);
        }
        return value;
    };

    juce::Image::BitmapData pixels(nextSpectrogramImage, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < width; ++x)
    {
        const auto& time = timeForX[static_cast<size_t>(x)];
        if (! time.valid)
            continue;
        for (int y = 0; y < height; ++y)
        {
            const auto yi = static_cast<size_t>(y);
            if (frequencyForY[yi].centreFrequency > static_cast<float>(map.sampleRate * 0.5))
                continue;
            const auto channel = useSecondChannelForY[yi] ? secondIndex : firstIndex;
            auto value = sampleHighQuality(channel, frequencyForY[yi], time);
            if (! useSplitView && useSecondSpectrum)
                value = std::max(value,
                    sampleHighQuality(secondIndex, frequencyForY[yi], time));
            pixels.setPixelColour(x, y, colourForValue(value, frequencyForY[yi].centreFrequency));
        }
    }

    offlineSpectrogramImage = std::move(nextSpectrogramImage);
    processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
    renderedOfflineMonitorMode = ana::spec::monitorModeIndex(monitorMode);
    renderedOfflineSplitView = useSplitView;
    repaint();
}
