#include "Display.h"
#include "shared/spec/Smoothing.h"
#include "shared/spec/SpectrumProcessing.h"

namespace ana::spec::display
{
void drawSpec(const PluginProcessor& processor, juce::Graphics& graphics,
                                const std::vector<float>& spec,
                                const int fftSize,
                                const double sampleRate,
                                const juce::Rectangle<float> plotBounds,
                                const juce::Colour lineColour,
                                const juce::Colour fillColour,
                                const bool allowFill,
                                const float gainDb)
{
    if (fftSize <= 0 || sampleRate <= 0.0 || spec.empty() || plotBounds.isEmpty())
        return;

    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));

    const auto smoothing = readParameterValue(processor, PluginProcessor::specSmoothingParameterId,
        ana::spectrum_processing::defaultSmoothingPercent);
    const auto shouldSmooth = ana::spectrum_processing::smoothingBandwidthOctaves(smoothing) > 0.0;
    std::vector<double> powers(spec.size(), 0.0);
    // Smooth the full FFT in power, before the display scale, zoom, slope or gain.
    for (size_t bin = 1; bin < spec.size(); ++bin)
    {
        if (! std::isfinite(spec[bin]))
            continue;
        const auto frequency = static_cast<float>(bin) * binFrequency;
        const auto value = shouldSmooth ? spec[bin]
            : spec[bin] + slope * std::log2(frequency / specSlopeReferenceFrequency) + gainDb;
        if (value > ana::spec::SpecProcessor::minimumDecibels)
            powers[bin] = std::pow(10.0, static_cast<double>(value) / 10.0);
    }

    const ana::spectrum_processing::FrequencySmoothing curve(std::move(powers), smoothing);
    const auto columns = curve.displayColumns(frequencyScale, lowFrequency, highFrequency,
                                              binFrequency, columnCount);
    std::vector<float> displayValues(columns.size(), lowRange);
    auto hasData = false;
    for (int column = 0; column < columnCount; ++column)
    {
        const auto index = static_cast<size_t>(column);
        const auto power = columns[index];
        if (! std::isfinite(power) || power <= 0.0)
            continue;
        auto value = static_cast<float>(10.0 * std::log10(power));
        if (shouldSmooth)
        {
            const auto frequency = ana::frequency_scale::frequencyAt(
                frequencyScale, lowFrequency, highFrequency,
                (static_cast<float>(column) + 0.5f) / static_cast<float>(columnCount));
            value += slope * std::log2(frequency / specSlopeReferenceFrequency) + gainDb;
        }
        displayValues[index] = value;
        hasData = hasData || value > lowRange + 0.01f;
    }
    if (! hasData)
        return;

    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    std::vector<juce::Point<int>> aliasedPoints;
    aliasedPoints.reserve(static_cast<size_t>(columnCount + 1));
    bool hasPoint = false;

    for (int column = 0; column < columnCount; ++column)
    {
        if (! std::isfinite(columns[static_cast<size_t>(column)]))
            continue;

        const auto displayValue = displayValues[static_cast<size_t>(column)];
        const auto normalisedLevel = juce::jlimit(0.0f, 1.0f,
            (displayValue - lowRange) / (highRange - lowRange));
        const auto point = juce::Point<float>(
            plotBounds.getX() + static_cast<float>(column) + 0.5f,
            plotBounds.getBottom() - normalisedLevel * plotBounds.getHeight());

        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            aliasedPoints.emplace_back(juce::roundToInt(firstPoint.x), juce::roundToInt(firstPoint.y));
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
            aliasedPoints.emplace_back(juce::roundToInt(point.x), juce::roundToInt(point.y));
        }
        lastPoint = point;
    }

    if (! hasPoint)
        return;

    const auto* antiAlias = processor.getRawParameterValue(
        PluginProcessor::specAntiAliasParameterId);
    const auto shouldAntiAlias = antiAlias == nullptr
        || antiAlias->load(std::memory_order_relaxed) >= 0.5f;
    const auto fillBaseline = plotBounds.getBottom();

    const auto* filled = processor.getRawParameterValue(
        PluginProcessor::specFilledDisplayParameterId);
    const auto shouldFill = allowFill
        && filled != nullptr
        && filled->load(std::memory_order_relaxed) >= 0.5f;

    if (shouldAntiAlias)
    {
        if (shouldFill)
        {
            auto fillPath = path;
            fillPath.lineTo(lastPoint.x, fillBaseline);
            fillPath.lineTo(firstPoint.x, fillBaseline);
            fillPath.closeSubPath();
            graphics.setColour(fillColour);
            graphics.fillPath(fillPath);
        }

        graphics.setColour(lineColour);
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
        return;
    }

    // JUCE line primitives stay anti-aliased; draw 1px rectangles for a truly aliased OFF mode.
    juce::Graphics::ScopedSaveState saveState(graphics);
    graphics.reduceClipRegion(plotBounds.toNearestInt());

    const auto rasteriseLine = [&graphics] (juce::Point<int> from, juce::Point<int> to)
    {
        auto x0 = from.x;
        auto y0 = from.y;
        const auto x1 = to.x;
        const auto y1 = to.y;
        const auto dx = std::abs(x1 - x0);
        const auto sx = x0 < x1 ? 1 : -1;
        const auto dy = -std::abs(y1 - y0);
        const auto sy = y0 < y1 ? 1 : -1;
        auto error = dx + dy;

        for (;;)
        {
            graphics.fillRect(x0, y0, 1, 1);
            if (x0 == x1 && y0 == y1)
                break;

            const auto twiceError = error * 2;
            if (twiceError >= dy)
            {
                error += dy;
                x0 += sx;
            }
            if (twiceError <= dx)
            {
                error += dx;
                y0 += sy;
            }
        }
    };

    if (shouldFill && ! aliasedPoints.empty())
    {
        const auto baselineY = juce::roundToInt(fillBaseline);
        graphics.setColour(fillColour);

        const auto fillColumn = [&graphics, baselineY] (const int x, const int y)
        {
            const auto top = std::min(y, baselineY);
            const auto bottom = std::max(y, baselineY);
            graphics.fillRect(x, top, 1, bottom - top + 1);
        };

        fillColumn(aliasedPoints.front().x, aliasedPoints.front().y);
        for (size_t index = 1; index < aliasedPoints.size(); ++index)
        {
            const auto from = aliasedPoints[index - 1];
            const auto to = aliasedPoints[index];
            const auto xDistance = to.x - from.x;

            if (xDistance == 0)
            {
                fillColumn(to.x, to.y);
                continue;
            }

            const auto step = xDistance > 0 ? 1 : -1;
            for (auto x = from.x; x != to.x + step; x += step)
            {
                const auto proportion = static_cast<float>(x - from.x)
                    / static_cast<float>(xDistance);
                const auto y = juce::roundToInt(juce::jmap(proportion,
                                                           static_cast<float>(from.y),
                                                           static_cast<float>(to.y)));
                fillColumn(x, y);
            }
        }
    }

    graphics.setColour(lineColour);
    if (aliasedPoints.size() == 1)
    {
        graphics.fillRect(aliasedPoints.front().x, aliasedPoints.front().y, 1, 1);
        return;
    }

    for (size_t index = 1; index < aliasedPoints.size(); ++index)
        rasteriseLine(aliasedPoints[index - 1], aliasedPoints[index]);
}
}
