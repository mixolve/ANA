#include "Display.h"

#include <algorithm>
#include <cmath>

namespace ana::scop::display
{
DisplayedModes getDisplayedModes(const ana::ScopChannelMode mode) noexcept
{
    switch (mode)
    {
        case ana::ScopChannelMode::lr: return { { 0, 1 }, 2 };
        case ana::ScopChannelMode::ms: return { { 2, 3 }, 2 };
        case ana::ScopChannelMode::left: return { { 0, 0 }, 1 };
        case ana::ScopChannelMode::right: return { { 1, 1 }, 1 };
        case ana::ScopChannelMode::mid: return { { 2, 2 }, 1 };
        case ana::ScopChannelMode::side: return { { 3, 3 }, 1 };
    }

    return { { 2, 2 }, 1 };
}

juce::String formatZoomValue(const float decibels)
{
    const auto displayValue = std::abs(decibels) < 0.05f ? 0.0f : decibels;
    return juce::String::formatted("%+.2f", displayValue);
}

void drawWaveformEnvelope(juce::Graphics& graphics,
                          const std::vector<float>& minimums,
                          const std::vector<float>& maximums,
                          const juce::Rectangle<float> bounds,
                          const float rangeStart,
                          const float rangeEnd,
                          const float verticalZoomDecibels,
                          const bool filledStyle)
{
    if (minimums.empty() || maximums.size() != minimums.size() || bounds.isEmpty())
        return;

    const auto sourceColumns = minimums.size();
    const auto visibleStart = juce::jlimit(0.0f, 0.999f, rangeStart);
    const auto visibleEnd = juce::jlimit(visibleStart + 0.001f, 1.0f, rangeEnd);
    const auto firstSourcePosition = visibleStart * static_cast<float>(sourceColumns - 1);
    const auto lastSourcePosition = visibleEnd * static_cast<float>(sourceColumns - 1);
    const auto visibleSourceLength = std::max(0.001f,
        lastSourcePosition - firstSourcePosition);
    const auto firstSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::floor(firstSourcePosition)));
    const auto lastSourceIndex = std::min(
        sourceColumns - 1,
        static_cast<size_t>(std::ceil(lastSourcePosition)));
    const auto centreY = bounds.getCentreY();
    const auto amplitude = std::max(0.0f, bounds.getHeight() * 0.5f);
    const auto zoomGain = juce::Decibels::decibelsToGain(verticalZoomDecibels);
    juce::Graphics::ScopedSaveState clipState(graphics);
    graphics.reduceClipRegion(bounds.toNearestInt());
    juce::Path path;
    std::vector<juce::Point<float>> lowerEdge;
    auto pointCount = 0;
    auto singleTop = 0.0f;

    const auto flushPath = [&]
    {
        if (pointCount == 1)
        {
            graphics.drawLine(lowerEdge.front().x, singleTop,
                              lowerEdge.front().x, lowerEdge.front().y, 1.0f);
        }
        else if (pointCount > 1)
        {
            for (auto iterator = lowerEdge.rbegin(); iterator != lowerEdge.rend(); ++iterator)
                path.lineTo(*iterator);

            path.closeSubPath();
            if (filledStyle)
                graphics.fillPath(path);
            else
                graphics.strokePath(path, juce::PathStrokeType(1.25f));
        }

        path.clear();
        lowerEdge.clear();
        pointCount = 0;
    };

    for (auto sourceIndex = firstSourceIndex;
         sourceIndex <= lastSourceIndex;
         ++sourceIndex)
    {
        auto minimum = minimums[sourceIndex];
        auto maximum = maximums[sourceIndex];

        if (minimum > maximum)
        {
            flushPath();
            continue;
        }

        minimum *= zoomGain;
        maximum *= zoomGain;
        const auto normalizedX =
            (static_cast<float>(sourceIndex) - firstSourcePosition) / visibleSourceLength;
        const auto x = bounds.getX() + normalizedX * bounds.getWidth();
        const auto top = centreY - maximum * amplitude;
        const auto bottom = centreY - minimum * amplitude;

        if (pointCount == 0)
        {
            path.startNewSubPath(x, top);
            singleTop = top;
        }
        else
        {
            path.lineTo(x, top);
        }

        lowerEdge.emplace_back(x, bottom);
        ++pointCount;
    }

    flushPath();
}
}
