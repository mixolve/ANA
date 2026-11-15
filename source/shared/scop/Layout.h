#pragma once

#include "shared/scop/Crossover.h"
#include "shared/shell/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ana::scop
{
inline constexpr int bandRangeSliderHeight = 14;
inline constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
inline int bandZoomValueWidth() noexcept
{
    return ana::ui::textControlWidth(6);
}
inline int waveformRightInset() noexcept
{
    return ana::ui::gap.pixels() + bandZoomSliderWidth;
}
inline constexpr int minimumRealtimeBandHeight = ana::ui::controlHeight
    + 2 * ana::ui::gap.pixels();
inline constexpr int minimumOfflineBandHeight = ana::ui::controlHeight
    + 3 * ana::ui::gap.pixels() + bandRangeSliderHeight;
// The top actions need one 24px row. Reserve a second row for the horizontal
// readouts and a separate row for the horizontal range only if enabled.
// The gaps are between the relevant rows, not permanently held when hidden.
constexpr int minimumBandHeightForControls(const bool horizontalZoom,
                                            const bool horizontalReadouts) noexcept
{
    return minimumRealtimeBandHeight
        + (horizontalReadouts ? ana::ui::controlHeight + ana::ui::gap.pixels() : 0)
        + (horizontalZoom ? bandRangeSliderHeight + ana::ui::gap.pixels() : 0);
}

constexpr int minimumEditorHeightForBands(const int minimumBandHeight,
                                          const size_t bandCount = ana::dsp::LinkwitzRileyCrossover::numBands) noexcept
{
    return ana::ui::gap.pixels() + ana::ui::controlHeight
        + static_cast<int>(bandCount) * minimumBandHeight;
}

using BandHeightWeights = std::array<float, dsp::LinkwitzRileyCrossover::numBands>;

struct BandLayout
{
    int width;
    int height;
    int minimumBandHeight;
    const BandHeightWeights& weights;
    // -1 shows all bands; full-source view uses the first band's controls.
    int singleBand = -1;

    juce::Rectangle<float> bounds(const size_t bandIndex, const size_t requestedBandCount) const noexcept
    {
        const auto bandCount = std::min(requestedBandCount, weights.size());
        if (bandCount == 0 || bandIndex >= bandCount)
            return {};
        const auto drawableWidth = static_cast<float>(std::max(0, width - 1));
        const auto availableHeight = static_cast<float>(std::max(0, height));
        if (singleBand >= 0)
            return singleBand == static_cast<int>(bandIndex)
                ? juce::Rectangle<float> { 0.0f, 0.0f, drawableWidth, availableHeight }
                : juce::Rectangle<float> {};

        auto totalWeight = 0.0f;
        auto precedingWeight = 0.0f;
        for (size_t index = 0; index < bandCount; ++index)
        {
            const auto weight = std::max(0.001f, weights[index]);
            totalWeight += weight;
            if (index < bandIndex)
                precedingWeight += weight;
        }
        const auto fixedHeight = std::min(static_cast<float>(minimumBandHeight),
                                          availableHeight / static_cast<float>(bandCount));
        const auto distributableHeight = std::max(0.0f,
            availableHeight - fixedHeight * static_cast<float>(bandCount));
        const auto top = fixedHeight * static_cast<float>(bandIndex)
            + distributableHeight * precedingWeight / totalWeight;
        const auto bottomWeight = precedingWeight + std::max(0.001f, weights[bandIndex]);
        const auto bottom = bandIndex + 1 == bandCount
            ? availableHeight
            : fixedHeight * static_cast<float>(bandIndex + 1)
                + distributableHeight * bottomWeight / totalWeight;
        return { 0.0f, top, drawableWidth, std::max(0.0f, bottom - top) };
    }

    int findSeparator(const int y, const size_t bandCount) const noexcept
    {
        if (singleBand >= 0)
            return -1;
        constexpr int hitRadius = 5;
        for (size_t band = 1; band < std::min(bandCount, weights.size()); ++band)
            if (std::abs(y - juce::roundToInt(bounds(band, bandCount).getY())) <= hitRadius)
                return static_cast<int>(band - 1);
        return -1;
    }

    bool resizeSeparator(BandHeightWeights& destination, const int separator,
                         const int y, const size_t bandCount) const noexcept
    {
        if (singleBand >= 0 || separator < 0
            || static_cast<size_t>(separator + 1) >= std::min(bandCount, weights.size()))
            return false;
        const auto upperIndex = static_cast<size_t>(separator);
        const auto lowerIndex = upperIndex + 1;
        const auto combinedTop = bounds(upperIndex, bandCount).getY();
        const auto combinedBottom = bounds(lowerIndex, bandCount).getBottom();
        const auto combinedHeight = combinedBottom - combinedTop;
        if (combinedHeight <= 1.0f)
            return false;
        const auto minimumHeight = std::min(static_cast<float>(minimumBandHeight),
                                            combinedHeight * 0.5f);
        const auto distributableHeight = combinedHeight - minimumHeight * 2.0f;
        if (distributableHeight <= 0.0f)
            return false;
        const auto separatorY = juce::jlimit(combinedTop + minimumHeight,
                                             combinedBottom - minimumHeight,
                                             static_cast<float>(y));
        const auto upperRatio = (separatorY - combinedTop - minimumHeight) / distributableHeight;
        const auto combinedWeight = weights[upperIndex] + weights[lowerIndex];
        destination[upperIndex] = combinedWeight * upperRatio;
        destination[lowerIndex] = combinedWeight * (1.0f - upperRatio);
        return true;
    }
};
}
