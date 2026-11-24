#include "View.h"
#include "SplitView.h"
#include "shared/spec/Display.h"
#include "shared/spec/Settings.h"
#include "shared/spec/MapFrequencyScale.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/GraphColours.h"
#include "shell/Processor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace ana::ui::analyzer_detail;
using namespace ana::spec::display;
using ana::spec::realtime_detail::shouldUseSplitView;

namespace
{
float sampleMapSpectrum(const std::vector<float>& spectrum,
                          const float binFrequency,
                          const float lowerFrequency,
                          const float centreFrequency,
                          const float upperFrequency,
                          const bool highQuality) noexcept
{
    constexpr float floorDb = ana::spec::SpecProcessor::minimumDecibels;
    if (spectrum.size() < 2 || binFrequency <= 0.0f)
        return floorDb;

    const auto maxBin = static_cast<int>(spectrum.size()) - 1;
    if (centreFrequency > static_cast<float>(maxBin) * binFrequency)
        return floorDb;
    const auto clampBin = [maxBin, binFrequency] (const float frequency)
    {
        return juce::jlimit(1.0f, static_cast<float>(maxBin), frequency / binFrequency);
    };
    const auto exactBin = clampBin(centreFrequency);
    const auto bin0 = static_cast<int>(std::floor(exactBin));
    const auto bin1 = std::min(maxBin, bin0 + 1);
    const auto mix = exactBin - static_cast<float>(bin0);
    auto value = spectrum[static_cast<size_t>(bin0)]
        + mix * (spectrum[static_cast<size_t>(bin1)] - spectrum[static_cast<size_t>(bin0)]);

    if (! highQuality)
        return value;

    // Max-bilinear sampling preserves narrow peaks when source bins collapse into one row.
    const auto firstCoord = std::min(clampBin(lowerFrequency), clampBin(upperFrequency));
    const auto lastCoord = std::max(clampBin(lowerFrequency), clampBin(upperFrequency));
    const auto first = std::max(1, static_cast<int>(std::ceil(firstCoord)));
    const auto last = std::min(maxBin, static_cast<int>(std::floor(lastCoord)));
    for (auto bin = first; bin <= last; ++bin)
        value = std::max(value, spectrum[static_cast<size_t>(bin)]);
    return value;
}
}

void RealtimeSpecView::resetSpectrogramImage()
{
    const auto bounds = getPlotBounds().toNearestInt();
    const auto width = std::max(1, bounds.getWidth());
    const auto height = std::max(1, bounds.getHeight());

    if (spectrogramImage.isValid()
        && spectrogramImage.getWidth() == width
        && spectrogramImage.getHeight() == height
        && realtimeSpectrogramLevels.size() == static_cast<size_t>(width * height))
        return;

    // A transient degenerate layout must not collapse the retained levels.
    if (spectrogramImage.isValid() && (width < 2 || height < 2))
        return;

    const auto oldWidth = spectrogramImage.isValid() ? spectrogramImage.getWidth() : 0;
    const auto oldHeight = spectrogramImage.isValid() ? spectrogramImage.getHeight() : 0;
    auto oldLevels = std::move(realtimeSpectrogramLevels);
    realtimeSpectrogramLevels.assign(static_cast<size_t>(width * height), ana::spec::SpecProcessor::minimumDecibels);
    if (oldWidth > 0 && oldHeight > 0
        && oldLevels.size() == static_cast<size_t>(oldWidth * oldHeight))
    {
        for (int y = 0; y < height; ++y)
        {
            const auto sourceY = juce::jlimit(0, oldHeight - 1,
                juce::roundToInt(static_cast<float>(y) * static_cast<float>(oldHeight - 1)
                                 / static_cast<float>(std::max(1, height - 1))));
            for (int x = 0; x < width; ++x)
            {
                const auto sourceX = juce::jlimit(0, oldWidth - 1,
                    juce::roundToInt(static_cast<float>(x) * static_cast<float>(oldWidth - 1)
                                     / static_cast<float>(std::max(1, width - 1))));
                realtimeSpectrogramLevels[static_cast<size_t>(y * width + x)] =
                    oldLevels[static_cast<size_t>(sourceY * oldWidth + sourceX)];
            }
        }
    }

    spectrogramImage = juce::Image(juce::Image::RGB, width, height, true);
    rebuildRealtimeSpectrogramImage();
}

void RealtimeSpecView::rebuildRealtimeSpectrogramImage()
{
    if (! spectrogramImage.isValid()
        || realtimeSpectrogramLevels.size()
            != static_cast<size_t>(spectrogramImage.getWidth() * spectrogramImage.getHeight()))
        return;

    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);

    juce::Image::BitmapData pixels(spectrogramImage, juce::Image::BitmapData::writeOnly);
    const auto width = spectrogramImage.getWidth();
    const auto height = spectrogramImage.getHeight();
    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto normalisedY = localHeight > 1
            ? 1.0f - static_cast<float>(localY) / static_cast<float>(localHeight - 1)
            : 0.0f;
        const auto frequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto rowOffset = static_cast<size_t>(y * width);
        for (int x = 0; x < width; ++x)
        {
            const auto rawValue = realtimeSpectrogramLevels[rowOffset + static_cast<size_t>(x)];
            const auto level = juce::jlimit(0.0f, 1.0f,
                (rawValue + slopeOffset - lowRange) / (highRange - lowRange));
            pixels.setPixelColour(x, y, spectrogramColour(level, colourMap));
        }
    }

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = readParameterValue(
        processor, PluginProcessor::specMapHighQualityRenderingParameterId, 1.0f) >= 0.5f ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);
}

void RealtimeSpecView::appendSpectrogramFrame(const int columnCount)
{
    resetSpectrogramImage();
    if (! spectrogramImage.isValid())
        return;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);
    const auto useSecondSpectrum = secondChannel != firstChannel;

    std::vector<float> firstSpectrum;
    std::vector<float> secondSpectrum;
    auto fftSize = 0;
    auto secondFftSize = 0;
    const auto sampleRate = processor.getSpecProcessor().getSampleRate();
    processor.getSpecProcessor().copyMapSpectrum(firstChannel, firstSpectrum, fftSize);
    if (useSecondSpectrum)
        processor.getSpecProcessor().copyMapSpectrum(secondChannel, secondSpectrum, secondFftSize);

    if (fftSize <= 0 || sampleRate <= 0.0 || firstSpectrum.empty())
        return;
    if (secondFftSize != fftSize || secondSpectrum.empty())
        secondSpectrum.clear();

    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto slope = displayRange.slope;
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specMapHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto width = spectrogramImage.getWidth();
    const auto height = spectrogramImage.getHeight();
    const auto columnsToAppend = juce::jlimit(1, width, columnCount);
    const auto leftToRight = readParameterValue(
        processor, PluginProcessor::specMapLeftToRightParameterId, 0.0f) >= 0.5f;
    const auto directionState = leftToRight ? 1 : 0;
    if (renderedMapLeftToRight != directionState)
    {
        clearSpectrogram();
        renderedMapLeftToRight = directionState;
    }

    auto firstTargetX = width - columnsToAppend;
    auto targetColumnCount = columnsToAppend;
    if (leftToRight)
    {
        if (realtimeMapWriteColumn + columnsToAppend > width)
            clearSpectrogram();
        firstTargetX = juce::jlimit(0, width - 1, realtimeMapWriteColumn);
        targetColumnCount = std::min(columnsToAppend, width - firstTargetX);
    }
    else if (width > columnsToAppend)
    {
        spectrogramImage.moveImageSection(0, 0, columnsToAppend, 0,
                                          width - columnsToAppend, height);
        for (int y = 0; y < height; ++y)
        {
            auto* row = realtimeSpectrogramLevels.data() + static_cast<size_t>(y * width);
            std::move(row + columnsToAppend, row + width, row);
        }
    }

    juce::Image::BitmapData pixels(spectrogramImage, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto normalisedY = localHeight > 1
            ? 1.0f - static_cast<float>(localY) / static_cast<float>(localHeight - 1)
            : 0.0f;
        const auto halfPixel = localHeight > 1
            ? 0.5f / static_cast<float>(localHeight - 1) : 0.0f;
        const auto lowerNormalised = juce::jlimit(0.0f, 1.0f, normalisedY - halfPixel);
        const auto upperNormalised = juce::jlimit(0.0f, 1.0f, normalisedY + halfPixel);
        const auto lowerBandFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, lowerNormalised);
        const auto upperBandFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, upperNormalised);
        const auto frequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);

        auto rawValue = sampleMapSpectrum(
            secondHalf ? secondSpectrum : firstSpectrum, binFrequency,
            lowerBandFrequency, frequency, upperBandFrequency, highQuality);
        if (! useSplitView && ! secondSpectrum.empty())
            rawValue = std::max(rawValue, sampleMapSpectrum(
                secondSpectrum, binFrequency, lowerBandFrequency, frequency,
                upperBandFrequency, highQuality));

        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto displayValue = rawValue + slopeOffset;
        const auto level = juce::jlimit(0.0f, 1.0f,
                                  (displayValue - lowRange) / (highRange - lowRange));
        const auto colour = spectrogramColour(level, colourMap);
        for (int column = 0; column < targetColumnCount; ++column)
        {
            const auto targetX = firstTargetX + column;
            realtimeSpectrogramLevels[static_cast<size_t>(y * width + targetX)] = rawValue;
            pixels.setPixelColour(targetX, y, colour);
        }
    }

    if (leftToRight)
        realtimeMapWriteColumn += targetColumnCount;

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = highQuality ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);
}
