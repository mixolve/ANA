#include "View.h"
#include "shared/lvls/Settings.h"
#include "shared/lvls/Processor.h"
#if ANA_VARIANT_RTM
#include "rtm/shell/Processor.h"
#else
#include "ara/shell/Processor.h"
#endif
#include "shared/shell/Theme.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int bandRangeSliderHeight = 14;
constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
constexpr int lvlsScaleLabelWidth = ana::ui::textControlWidth(3);
constexpr int lvlsReadoutWidth = ana::ui::textControlWidth(7);
constexpr int historyMetricReadoutWidth = ana::ui::textControlWidth(7);
constexpr int historySviewWidth = ana::ui::textControlWidth(5);
constexpr int historyMinimumWidth = historyMetricReadoutWidth * 2 + historySviewWidth + bandZoomSliderWidth
    + ana::ui::gap.pixels() * 3;

struct LvlsHistoryLayout
{
    juce::Rectangle<int> header;
    juce::Rectangle<int> plot;
    juce::Rectangle<int> metricLabelRow;
    juce::Rectangle<int> metricReadoutRow;
    juce::Rectangle<int> sviewButton;
    juce::Rectangle<int> horizontalZoom;
    juce::Rectangle<int> verticalZoom;
};

LvlsHistoryLayout makeLvlsHistoryLayout(juce::Rectangle<int> bounds,
                                          const bool showZoomControls) noexcept
{
    LvlsHistoryLayout layout;
    auto historyArea = bounds;
    if (showZoomControls)
    {
        layout.verticalZoom = historyArea.removeFromRight(
            std::min(bandZoomSliderWidth, historyArea.getWidth()));
        ana::ui::gap.removeFromRight(historyArea);
    }

    layout.header = historyArea.removeFromTop(
        std::min(ana::ui::controlHeight, historyArea.getHeight()));
    ana::ui::gap.removeFromTop(historyArea);

    auto plotArea = historyArea;
    if (showZoomControls)
    {
        layout.horizontalZoom = plotArea.removeFromBottom(
            std::min(bandRangeSliderHeight, plotArea.getHeight()));
        ana::ui::gap.removeFromBottom(plotArea);
    }
    layout.plot = plotArea;

    const auto sviewWidth = layout.plot.getWidth() >= historySviewWidth ? historySviewWidth : 0;
    const auto sviewY = std::max(layout.plot.getY(),
        layout.plot.getBottom() - ana::ui::controlHeight);
    layout.sviewButton = {
        layout.plot.getX(),
        sviewY,
        sviewWidth,
        std::min(ana::ui::controlHeight, layout.plot.getBottom() - sviewY)
    };
    layout.metricLabelRow = historyArea.withWidth(historyMetricReadoutWidth * 2
                                             + ana::ui::gap.pixels())
                                      .withHeight(std::min(ana::ui::controlHeight,
                                                          historyArea.getHeight()));
    layout.metricReadoutRow = historyArea.withWidth(historyMetricReadoutWidth * 2
                                               + ana::ui::gap.pixels())
        .withHeight(layout.metricLabelRow.getHeight())
        .withY(layout.metricLabelRow.getBottom() + ana::ui::gap.pixels());
    return layout;
}

juce::String formatReadoutLevel(const double level)
{
    return juce::String::formatted("%+07.2f", level);
}

juce::String formatLufsReadout(const double level)
{
    return level <= -119.9 ? juce::String("-inf") : formatReadoutLevel(level);
}

juce::String formatLevelScaleTick(const int value)
{
    return juce::String::formatted("%+03d", value);
}

}

LvlsView::LvlsView(PluginProcessor& processorRef)
    : processor(processorRef)
{
    partWeights = processor.getLvlsSectionWeights();
    peakModeButton.onClick = [this]
    {
        showPeakLvls = true;
        peakModeButton.setToggleState(true, juce::dontSendNotification);
        rmsModeButton.setToggleState(false, juce::dontSendNotification);
        repaint();
    };
    rmsModeButton.onClick = [this]
    {
        showPeakLvls = false;
        peakModeButton.setToggleState(false, juce::dontSendNotification);
        rmsModeButton.setToggleState(true, juce::dontSendNotification);
        repaint();
    };
    peakChannelModeButton.onClick = [this]
    {
        showMidSideLvls = ! showMidSideLvls;
        peakChannelModeButton.setToggleState(showMidSideLvls, juce::dontSendNotification);
        repaint();
    };
    historySviewButton.onClick = [this]
    {
        historySolo = ! historySolo;
        historySviewButton.setToggleState(historySolo, juce::dontSendNotification);
        resized();
    };
    historyHorizontalZoom.onRangeChanged = [this] { repaint(); };
    historyVerticalZoom.onRangeChanged = [this] { repaint(); };
    peakModeButton.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(peakModeButton);
    addAndMakeVisible(rmsModeButton);
    addAndMakeVisible(peakChannelModeButton);
    addAndMakeVisible(historySviewButton);
    addAndMakeVisible(historyHorizontalZoom);
    addAndMakeVisible(historyVerticalZoom);
    startTimerHz(30);
}

void LvlsView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);

    const auto minimumLvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth);
    const auto parts = getPartBounds();
    const auto visibleParts = getVisibleParts();
    auto peakRmsBounds = parts[0];
    auto lufsBounds = parts[1];
    const auto historyBounds = (historySolo && visibleParts[2]
        ? getLocalBounds() : parts[2]).toFloat();
    const auto scaleSideWidth = lvlsScaleLabelWidth + ana::ui::gap.pixels();
    const auto peakScalesVisible = peakRmsBounds.getWidth()
        >= minimumLvlsColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto peakLvlsHorizontalBounds = peakRmsBounds;
    if (peakScalesVisible)
        peakLvlsHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto peakLvlsWidth = std::max(1,
        (peakLvlsHorizontalBounds.getWidth() - ana::ui::gap.pixels()) / 2);
    const auto lufsScalesVisible = lufsBounds.getWidth()
        >= minimumLvlsColumnWidth * 3 + ana::ui::gap.pixels() * 2 + scaleSideWidth * 2;
    auto lufsLvlsHorizontalBounds = lufsBounds;
    if (lufsScalesVisible)
        lufsLvlsHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto lufsLvlsWidth = std::max(1,
        (lufsLvlsHorizontalBounds.getWidth() - ana::ui::gap.pixels() * 2) / 3);
    const auto peakChannelOffset = showMidSideLvls ? size_t { 2 } : size_t { 0 };

    const auto displayFont = ana::ui::makeFont();
    const auto readLevelScale = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto peakLow = readLevelScale(PluginProcessor::lvlsPeakRangeLowParameterId, ana::lvls::defaultDisplayLowDecibels);
    const auto peakHigh = std::max(peakLow + 0.1f,
                                   readLevelScale(PluginProcessor::lvlsPeakRangeHighParameterId, ana::lvls::defaultDisplayHighDecibels));
    const auto lufsLow = readLevelScale(PluginProcessor::lvlsLoudnessRangeLowParameterId, ana::lvls::defaultDisplayLowDecibels);
    const auto lufsHigh = std::max(lufsLow + 0.1f,
                                   readLevelScale(PluginProcessor::lvlsLoudnessRangeHighParameterId, ana::lvls::defaultDisplayHighDecibels));
    const auto normalise = [] (const float value, const float low, const float high)
    {
        return juce::jlimit(0.0f, 1.0f, (value - low) / (high - low));
    };
    const auto drawScale = [&] (const juce::Rectangle<float> bounds, const float low, const float high,
                                const bool drawLeftLabels, const bool drawRightLabels,
                                const bool lufsScale)
    {
        graphics.setFont(displayFont);
        const auto span = high - low;
        const auto minorStep = span <= 12.0f ? 1 : span <= 24.0f ? 2 : span <= 48.0f ? 5 : 10;
        const auto minimumLabelSpacing = static_cast<float>(ana::ui::baseFontSize
            + ana::ui::gap.pixels() / 2);
        const auto maximumLabelIntervals = std::max(2, 1 + static_cast<int>(std::floor(
            bounds.getHeight() / std::max(1.0f, minimumLabelSpacing))));
        const auto requiredMajorStep = span / static_cast<float>(maximumLabelIntervals);
        const auto chooseMajorMultiplier = [] (const float minimumMultiplier)
        {
            for (const auto candidate : std::array<int, 8> { 1, 2, 3, 5, 10, 20, 50, 100 })
                if (static_cast<float>(candidate) >= minimumMultiplier)
                    return candidate;
            return 100;
        };
        const auto majorStep = minorStep * chooseMajorMultiplier(
            requiredMajorStep / static_cast<float>(minorStep));
        const auto highestTick = static_cast<int>(std::floor(high / static_cast<float>(minorStep))) * minorStep;
        const auto lowestTick = static_cast<int>(std::ceil(low / static_cast<float>(minorStep))) * minorStep;
        for (auto tick = highestTick; tick >= lowestTick; tick -= minorStep)
        {
            const auto value = static_cast<float>(tick);
            const auto y = bounds.getBottom() - normalise(value, low, high) * bounds.getHeight();
            const auto isMajorTick = tick % majorStep == 0;
            if (! isMajorTick)
                continue;
            const auto lineY = juce::roundToInt(y);
            graphics.setColour(ana::ui::dark);
            graphics.fillRect(juce::roundToInt(bounds.getX()), lineY,
                              juce::roundToInt(bounds.getWidth()), 1);

            const auto labelY = lineY - ana::ui::controlHeight / 2;
            const auto label = lufsScale && tick <= -120 ? juce::String("-inf")
                                                          : formatLevelScaleTick(tick);
            if (drawLeftLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getX()) - ana::ui::gap.pixels() - lvlsScaleLabelWidth,
                                  labelY, lvlsScaleLabelWidth, ana::ui::controlHeight,
                                  juce::Justification::centredRight, true);
            if (drawRightLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getRight()) + ana::ui::gap.pixels(),
                                  labelY, lvlsScaleLabelWidth, ana::ui::controlHeight,
                                  juce::Justification::centredLeft, true);
        }
    };
    const auto drawPeakRmsLvls = [&] (juce::Rectangle<int> bounds, const size_t channel,
                                       const bool drawLeftScale, const bool drawRightScale)
    {
        const auto showReadout = bounds.getHeight() >= ana::ui::controlHeight * 4;
        auto readoutBounds = bounds.removeFromBottom(ana::ui::controlHeight);
        ana::ui::gap.removeFromBottom(bounds);
        const auto barFrame = bounds;
        auto bar = barFrame.toFloat().reduced(1.0f, 0.0f);
        if (bar.isEmpty())
            return;
        graphics.setColour(ana::ui::black);
        graphics.fillRect(bar);
        const auto lvlsValue = showPeakLvls
            ? peakValues[channel] : rmsValues[channel];
        const auto lvlsTop = bar.getBottom()
            - normalise(lvlsValue, peakLow, peakHigh) * bar.getHeight();
        graphics.setColour(ana::ui::light);
        graphics.fillRect(bar.getX(), lvlsTop, bar.getWidth(), bar.getBottom() - lvlsTop);
        graphics.setColour(ana::ui::white);
        graphics.fillRect(bar.getX(), lvlsTop, bar.getWidth(), 1.0f);
        if (showPeakLvls)
        {
            const auto holdY = bar.getBottom()
                - normalise(peakHoldValues[channel], peakLow, peakHigh) * bar.getHeight();
            graphics.setColour(ana::ui::white);
            graphics.fillRect(bar.getX(), holdY - 0.5f, bar.getWidth(), 2.0f);
        }
        drawScale(bar, peakLow, peakHigh, drawLeftScale, drawRightScale, false);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(barFrame, 1);
        if (showReadout)
        {
            graphics.setColour(ana::ui::light);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(lvlsValue <= -119.95f ? juce::String("-inf")
                                                     : formatReadoutLevel(lvlsValue), ana::ui::readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };
    const auto drawLufsLvls = [&] (juce::Rectangle<int> bounds, const juce::String& title,
                                    const float value, const float maximum, const bool drawLeftScale,
                                    const bool drawRightScale)
    {
        auto titleBounds = bounds.removeFromTop(ana::ui::controlHeight);
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(titleBounds, 1);
        graphics.setColour(ana::ui::white);
        graphics.drawText(title, titleBounds, juce::Justification::centred, true);
        ana::ui::gap.removeFromTop(bounds);
        auto maximumBounds = bounds.removeFromTop(ana::ui::controlHeight);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(maximumBounds, 1.0f);
        graphics.setColour(ana::ui::white);
        graphics.drawText(maximum <= -119.95f ? juce::String("-inf") : formatLufsReadout(maximum),
                          ana::ui::readoutTextBounds(maximumBounds), juce::Justification::centred, true);
        ana::ui::gap.removeFromTop(bounds);
        const auto showReadout = bounds.getHeight() >= ana::ui::controlHeight * 4;
        auto readoutBounds = bounds.removeFromBottom(ana::ui::controlHeight);
        ana::ui::gap.removeFromBottom(bounds);
        const auto barFrame = bounds;
        auto bar = barFrame.toFloat().reduced(1.0f, 0.0f);
        if (bar.isEmpty())
            return;
        graphics.setColour(ana::ui::black);
        graphics.fillRect(bar);
        const auto fillRange = [&] (const float low, const float high, const juce::Colour colour)
        {
            const auto fillLow = juce::jlimit(low, high, value);
            if (fillLow <= low)
                return;
            const auto top = bar.getBottom()
                - normalise(fillLow, lufsLow, lufsHigh) * bar.getHeight();
            const auto bottom = bar.getBottom()
                - normalise(low, lufsLow, lufsHigh) * bar.getHeight();
            graphics.setColour(colour);
            graphics.fillRect(bar.getX(), top, bar.getWidth(), bottom - top);
        };
        fillRange(-60.0f, -23.0f, ana::ui::dark);
        fillRange(-23.0f, -14.0f, ana::ui::light);
        fillRange(-14.0f, 0.0f, ana::ui::white);
        const auto markerY = bar.getBottom()
            - normalise(value, lufsLow, lufsHigh) * bar.getHeight();
        graphics.setColour(ana::ui::white);
        graphics.fillRect(bar.getX(), markerY, bar.getWidth(), 1.0f);
        drawScale(bar, lufsLow, lufsHigh, drawLeftScale, drawRightScale, true);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(barFrame, 1);
        if (showReadout)
        {
            graphics.setColour(ana::ui::light);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(value <= -119.95f ? juce::String("-inf")
                                                : formatLufsReadout(value), ana::ui::readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };

    if (! historySolo)
    {
        if (visibleParts[0])
        {
        peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        ana::ui::gap.removeFromTop(peakRmsBounds);
        const auto peakGroupWidth = peakLvlsHorizontalBounds.getWidth();
        auto peakLabelBounds = peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        peakLabelBounds.setX(peakLvlsHorizontalBounds.getX());
        peakLabelBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakLabelRow(peakLabelBounds);
        const std::array<juce::String, 2> peakLabels = showMidSideLvls
            ? std::array<juce::String, 2> { "M", "S" }
            : std::array<juce::String, 2> { "L", "R" };
        for (const auto& label : peakLabels)
        {
            auto labelBounds = peakLabelRow.takeLeft(peakLvlsWidth);
            graphics.setColour(ana::ui::light);
            graphics.drawRect(labelBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(label, labelBounds, juce::Justification::centred, true);
        }
        ana::ui::gap.removeFromTop(peakRmsBounds);
        auto peakMaximumBounds = peakRmsBounds.removeFromTop(ana::ui::controlHeight);
        peakMaximumBounds.setX(peakLvlsHorizontalBounds.getX());
        peakMaximumBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakMaximumRow(peakMaximumBounds);
        for (size_t channel = 0; channel < 2; ++channel)
        {
            const auto maximumBounds = peakMaximumRow.takeLeft(peakLvlsWidth);
            const auto valueIndex = peakChannelOffset + channel;
            const auto maximum = showPeakLvls ? peakMaximumValues[valueIndex] : rmsMaximumValues[valueIndex];
            graphics.setColour(ana::ui::light);
            graphics.drawRect(maximumBounds, 1.0f);
            graphics.setColour(ana::ui::white);
            graphics.drawText(maximum <= -119.95f ? juce::String("-inf") : formatReadoutLevel(maximum),
                              ana::ui::readoutTextBounds(maximumBounds), juce::Justification::centred, true);
        }
        ana::ui::gap.removeFromTop(peakRmsBounds);
        peakRmsBounds.setX(peakLvlsHorizontalBounds.getX());
        peakRmsBounds.setWidth(peakGroupWidth);
        ana::ui::FixedGapRow peakRow(peakRmsBounds);
        drawPeakRmsLvls(peakRow.takeLeft(peakLvlsWidth), peakChannelOffset, peakScalesVisible, false);
        drawPeakRmsLvls(peakRow.takeLeft(peakLvlsWidth), peakChannelOffset + 1, false, peakScalesVisible);
        }

        if (visibleParts[1])
        {
        const auto lufsGroupWidth = lufsLvlsHorizontalBounds.getWidth();
        auto loudnessHeader = lufsLvlsHorizontalBounds.removeFromTop(ana::ui::controlHeight);
        graphics.setColour(ana::ui::light);
        graphics.drawRect(loudnessHeader, 1.0f);
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::white);
        graphics.drawText("LOUDNESS", loudnessHeader, juce::Justification::centred, true);
        lufsBounds.removeFromTop(ana::ui::controlHeight);
        ana::ui::gap.removeFromTop(lufsBounds);
        lufsBounds.setX(lufsLvlsHorizontalBounds.getX());
        lufsBounds.setWidth(lufsGroupWidth);
        ana::ui::FixedGapRow lufsRow(lufsBounds);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "M", momentaryLufs, momentaryMaximumLufs,
                      lufsScalesVisible, false);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "S", shortTermLufs, shortTermMaximumLufs, false, false);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "I", integratedLufs, integratedMaximumLufs,
                      false, lufsScalesVisible);
        }

        auto previousVisible = -1;
        for (size_t index = 0; index < visibleParts.size(); ++index)
        {
            if (! visibleParts[index])
                continue;
            if (previousVisible < 0)
            {
                previousVisible = static_cast<int>(index);
                continue;
            }
            const auto x = static_cast<float>(
                (parts[static_cast<size_t>(previousVisible)].getRight()
                 + parts[index].getX()) / 2);
            const auto activeSeparator = hoveredPartSeparator == previousVisible
                || draggedPartSeparator == previousVisible;
            graphics.setColour(activeSeparator ? ana::ui::white : ana::ui::light);
            graphics.fillRect(x, 0.0f, 1.0f, static_cast<float>(getHeight()));
            previousVisible = static_cast<int>(index);
        }
    }

    if (historyBounds.isEmpty())
        return;

    const auto* historyZoomParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::lvlsHistoryZoomParameterId);
    const auto historyZoomVisible = historyZoomParameter == nullptr
        || historyZoomParameter->load(std::memory_order_relaxed) >= 0.5f;
    const auto historyLayout = makeLvlsHistoryLayout(historyBounds.toNearestInt(),
                                                       historyZoomVisible);
    graphics.setFont(displayFont);
    graphics.setColour(ana::ui::light);
    graphics.drawRect(historyLayout.header, 1.0f);
    graphics.setColour(ana::ui::white);
    graphics.drawText("HISTORY", historyLayout.header,
                      juce::Justification::centred, true);
    const auto drawHistoryMetrics = [&]
    {
        const auto drawMetric = [&] (const juce::String& label, const juce::String& readout,
                                     const juce::Rectangle<int> labelBounds,
                                     const juce::Rectangle<int> readoutBounds)
        {
            graphics.setColour(ana::ui::light);
            graphics.drawRect(labelBounds, 1.0f);
            graphics.drawRect(readoutBounds, 1.0f);
            graphics.setColour(ana::ui::white);
            graphics.drawText(label, labelBounds, juce::Justification::centred, true);
            graphics.drawText(readout, ana::ui::readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        };
        ana::ui::FixedGapRow metricLabels(historyLayout.metricLabelRow);
        ana::ui::FixedGapRow metricReadouts(historyLayout.metricReadoutRow);
        const auto truePeak = std::max(peakMaximumValues[0], peakMaximumValues[1]);
        drawMetric("TP", truePeak <= -119.95f ? juce::String("-inf") : formatReadoutLevel(truePeak),
                   metricLabels.takeLeft(historyMetricReadoutWidth),
                   metricReadouts.takeLeft(historyMetricReadoutWidth));
        drawMetric("LRA", formatReadoutLevel(loudnessRange),
                   metricLabels.takeLeft(historyMetricReadoutWidth),
                   metricReadouts.takeLeft(historyMetricReadoutWidth));
    };

    const auto historyEnabled = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const std::array<bool, 3> visibleHistorySeries {
        historyEnabled(PluginProcessor::lvlsHistoryMomentaryVisibleParameterId),
        historyEnabled(PluginProcessor::lvlsHistoryShortTermVisibleParameterId),
        historyEnabled(PluginProcessor::lvlsHistoryIntegratedVisibleParameterId)
    };
    const auto historySize = loudnessHistories[0].size();
    const auto plotBounds = historyLayout.plot.toFloat();
    if (historySize < 2 || plotBounds.isEmpty())
    {
        drawHistoryMetrics();
        return;
    }

    const auto visibleStart = juce::jlimit(0.0f, 0.999f, historyHorizontalZoom.getRangeStart());
    const auto visibleEnd = juce::jlimit(visibleStart + 0.001f, 1.0f,
                                         historyHorizontalZoom.getRangeEnd());
    const auto firstPosition = visibleStart * static_cast<float>(historySize - 1);
    const auto lastPosition = visibleEnd * static_cast<float>(historySize - 1);
    const auto visibleLength = std::max(0.001f, lastPosition - firstPosition);
    const auto visibleHigh = juce::jmap(historyVerticalZoom.getRangeStart(), lufsHigh, lufsLow);
    const auto visibleLow = juce::jmap(historyVerticalZoom.getRangeEnd(), lufsHigh, lufsLow);
    const auto historyPoint = [&] (const float position, const float value)
    {
        return juce::Point<float> {
            plotBounds.getX() + (position - firstPosition) / visibleLength * plotBounds.getWidth(),
            plotBounds.getBottom() - normalise(value, visibleLow, visibleHigh) * plotBounds.getHeight()
        };
    };
    const auto drawHistorySeries = [&] (const size_t series, const juce::Colour colour,
                                        const bool fill)
    {
        const auto& history = loudnessHistories[series];
        if (! visibleHistorySeries[series] || history.size() != historySize)
            return;
        const auto valueAt = [&] (const float position)
        {
            const auto first = std::min(history.size() - 1,
                static_cast<size_t>(std::floor(position)));
            const auto second = std::min(history.size() - 1, first + 1);
            return juce::jmap(position - static_cast<float>(first), history[first], history[second]);
        };
        juce::Path path;
        path.startNewSubPath(historyPoint(firstPosition, valueAt(firstPosition)));
        const auto firstIndex = static_cast<size_t>(std::ceil(firstPosition));
        const auto lastIndex = static_cast<size_t>(std::floor(lastPosition));
        for (auto index = firstIndex; index <= lastIndex && index < history.size(); ++index)
            if (static_cast<float>(index) > firstPosition && static_cast<float>(index) < lastPosition)
                path.lineTo(historyPoint(static_cast<float>(index), history[index]));
        path.lineTo(historyPoint(lastPosition, valueAt(lastPosition)));
        if (fill)
        {
            auto fillPath = path;
            fillPath.lineTo(plotBounds.getRight(), plotBounds.getBottom());
            fillPath.lineTo(plotBounds.getX(), plotBounds.getBottom());
            fillPath.closeSubPath();
            graphics.setColour(ana::ui::dark);
            graphics.fillPath(fillPath);
        }
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
    };
    drawHistorySeries(ana::lvls::LvlsProcessor::integratedHistory, ana::ui::white, true);
    drawHistorySeries(ana::lvls::LvlsProcessor::shortTermHistory, ana::ui::light, false);
    drawHistorySeries(ana::lvls::LvlsProcessor::momentaryHistory, ana::ui::white, false);
    drawHistoryMetrics();

}

void LvlsView::resized()
{
    const auto visibleParts = getVisibleParts();
    if (! visibleParts[2])
    {
        historySolo = false;
        historySviewButton.setToggleState(false, juce::dontSendNotification);
    }
    layoutPeakModeButtons();
    const auto historyBounds = historySolo ? getLocalBounds() : getPartBounds()[2];
    const auto* historyZoomParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::lvlsHistoryZoomParameterId);
    const auto historyZoomVisible = historyZoomParameter == nullptr
        || historyZoomParameter->load(std::memory_order_relaxed) >= 0.5f;
    const auto historyLayout = makeLvlsHistoryLayout(historyBounds, historyZoomVisible);
    historySviewButton.setVisible(visibleParts[2]);
    historyHorizontalZoom.setVisible(visibleParts[2] && historyZoomVisible);
    historyVerticalZoom.setVisible(visibleParts[2] && historyZoomVisible);
    historySviewButton.setBounds(historyLayout.sviewButton);
    historyHorizontalZoom.setBounds(historyLayout.horizontalZoom);
    historyVerticalZoom.setBounds(historyLayout.verticalZoom);
    repaint();
}

void LvlsView::centerParts()
{
    partWeights.fill(1.0f);
    processor.setLvlsSectionWeights(partWeights);
    draggedPartSeparator = -1;
    resized();
}

void LvlsView::layoutPeakModeButtons()
{
    const auto showPeakControls = ! historySolo && getVisibleParts()[0];
    peakModeButton.setVisible(showPeakControls);
    rmsModeButton.setVisible(showPeakControls);
    peakChannelModeButton.setVisible(showPeakControls);
    if (! showPeakControls)
        return;

    const auto partBounds = getPartBounds()[0];
    const auto minimumLvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth);
    const auto scaleSideWidth = lvlsScaleLabelWidth + ana::ui::gap.pixels();
    const auto scalesVisible = partBounds.getWidth()
        >= minimumLvlsColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto headerBounds = partBounds;
    if (scalesVisible)
        headerBounds.reduce(scaleSideWidth, 0);
    headerBounds = headerBounds.removeFromTop(ana::ui::controlHeight);
    const auto availableButtonWidth = std::max(0,
        headerBounds.getWidth() - ana::ui::gap.pixels() * 2);
    const auto preferredWidth = peakModeButton.getPreferredWidth()
        + rmsModeButton.getPreferredWidth() + peakChannelModeButton.getPreferredWidth();
    const auto extraPerButton = std::max(0, availableButtonWidth - preferredWidth) / 3;
    ana::ui::FixedGapRow buttonRow(headerBounds);
    peakModeButton.setBounds(buttonRow.takeLeft(
        peakModeButton.getPreferredWidth() + extraPerButton));
    rmsModeButton.setBounds(buttonRow.takeLeft(
        rmsModeButton.getPreferredWidth() + extraPerButton));
    peakChannelModeButton.setBounds(buttonRow.remaining().getWidth() >= peakChannelModeButton.getPreferredWidth()
        ? buttonRow.remaining() : juce::Rectangle<int>());
}

int LvlsView::getLvlsWidth() const noexcept
{
    const auto* parameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::lvlsWidthParameterId);
    return juce::jlimit(ana::lvls::minimumMeterWidth, ana::lvls::maximumMeterWidth,
                        juce::roundToInt(parameter != nullptr
                            ? parameter->load(std::memory_order_relaxed)
                            : static_cast<float>(ana::lvls::defaultMeterWidth)));
}

std::array<bool, 3> LvlsView::getVisibleParts() const noexcept
{
    const auto enabled = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    return {
        enabled(PluginProcessor::lvlsPeakRmsVisibleParameterId),
        enabled(PluginProcessor::lvlsLoudnessVisibleParameterId),
        enabled(PluginProcessor::lvlsHistoryVisibleParameterId)
    };
}

std::array<int, 3> LvlsView::getMinimumPartWidths() const noexcept
{
    const auto lvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth);
    return {
        lvlsColumnWidth * 2 + ana::ui::gap.pixels(),
        lvlsColumnWidth * 3 + ana::ui::gap.pixels() * 2,
        historyMinimumWidth
    };
}

std::array<juce::Rectangle<int>, 3> LvlsView::getPartBounds() const noexcept
{
    std::array<juce::Rectangle<int>, 3> bounds;
    const auto visible = getVisibleParts();
    const auto visibleCount = static_cast<int>(std::count(visible.begin(), visible.end(), true));
    if (visibleCount == 0)
        return bounds;

    auto remaining = getLocalBounds();
    const auto usableWidth = std::max(0, remaining.getWidth()
        - ana::ui::gap.pixels() * (visibleCount - 1));
    const auto minimumWidths = getMinimumPartWidths();
    auto minimumTotal = 0;
    auto totalWeight = 0.0f;
    for (size_t index = 0; index < visible.size(); ++index)
        if (visible[index])
        {
            minimumTotal += minimumWidths[index];
            totalWeight += partWeights[index];
        }
    std::array<int, 3> widths {};

    if (usableWidth < minimumTotal)
    {
        const auto scale = minimumTotal > 0
            ? static_cast<float>(usableWidth) / static_cast<float>(minimumTotal) : 0.0f;
        auto assignedWidth = 0;
        auto lastVisible = size_t { 0 };
        for (size_t index = 0; index < visible.size(); ++index)
            if (visible[index])
                lastVisible = index;
        for (size_t index = 0; index < visible.size(); ++index)
            if (visible[index])
            {
                widths[index] = index == lastVisible
                    ? std::max(0, usableWidth - assignedWidth)
                    : juce::roundToInt(static_cast<float>(minimumWidths[index]) * scale);
                assignedWidth += widths[index];
            }
    }
    else
    {
        std::array<bool, 3> fixed {};
        auto remainingWidth = usableWidth;
        auto remainingWeight = std::max(0.001f, totalWeight);

        for (size_t pass = 0; pass < widths.size(); ++pass)
        {
            auto fixedOne = false;
            for (size_t index = 0; index < widths.size(); ++index)
            {
                if (! visible[index] || fixed[index])
                    continue;

                const auto weightedWidth = static_cast<float>(remainingWidth)
                    * partWeights[index] / remainingWeight;
                if (weightedWidth >= static_cast<float>(minimumWidths[index]))
                    continue;

                widths[index] = minimumWidths[index];
                remainingWidth -= widths[index];
                remainingWeight -= partWeights[index];
                fixed[index] = true;
                fixedOne = true;
            }

            if (! fixedOne)
                break;
        }

        auto lastFlexible = widths.size();
        for (size_t index = 0; index < widths.size(); ++index)
            if (visible[index] && ! fixed[index])
                lastFlexible = index;

        for (size_t index = 0; index < widths.size(); ++index)
        {
            if (! visible[index] || fixed[index])
                continue;

            if (index == lastFlexible)
                widths[index] = remainingWidth;
            else
            {
                widths[index] = juce::roundToInt(static_cast<float>(remainingWidth)
                    * partWeights[index] / remainingWeight);
                remainingWidth -= widths[index];
                remainingWeight -= partWeights[index];
            }
        }
    }

    auto placed = 0;
    for (size_t index = 0; index < bounds.size(); ++index)
    {
        if (! visible[index])
            continue;
        bounds[index] = remaining.removeFromLeft(std::min(widths[index], remaining.getWidth()));
        ++placed;
        if (placed < visibleCount)
            ana::ui::gap.removeFromLeft(remaining);
    }
    return bounds;
}

int LvlsView::findPartSeparator(const int x) const noexcept
{
    const auto parts = getPartBounds();
    const auto visible = getVisibleParts();
    auto previous = -1;
    for (size_t index = 0; index < visible.size(); ++index)
    {
        if (! visible[index])
            continue;
        if (previous < 0)
        {
            previous = static_cast<int>(index);
            continue;
        }
        const auto separatorX = (parts[static_cast<size_t>(previous)].getRight()
                                 + parts[index].getX()) / 2;
        if (std::abs(x - separatorX) <= ana::ui::gap.pixels())
            return previous;
        previous = static_cast<int>(index);
    }

    return -1;
}

void LvlsView::mouseMove(const juce::MouseEvent& event)
{
    const auto nextHoveredSeparator = findPartSeparator(event.x);
    if (hoveredPartSeparator == nextHoveredSeparator)
        return;

    hoveredPartSeparator = nextHoveredSeparator;
    setMouseCursor(hoveredPartSeparator >= 0
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

void LvlsView::mouseExit(const juce::MouseEvent&)
{
    if (draggedPartSeparator >= 0)
        return;

    hoveredPartSeparator = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void LvlsView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != this)
        return;

    const auto parts = getPartBounds();
    draggedPartSeparator = findPartSeparator(event.x);
    if (draggedPartSeparator < 0)
    {
       #if ANA_VARIANT_RTM
        if (! processor.getLvlsProcessor().isFrozen()
            && ! historySolo
            && (parts[0].contains(event.getPosition()) || parts[1].contains(event.getPosition())))
            processor.clearLvlsProcessor();
       #endif
        return;
    }

    draggedLeftPart = draggedPartSeparator;
    draggedRightPart = -1;
    const auto visible = getVisibleParts();
    for (auto part = draggedLeftPart + 1; part < static_cast<int>(visible.size()); ++part)
        if (visible[static_cast<size_t>(part)])
        {
            draggedRightPart = part;
            break;
        }
    if (draggedRightPart < 0)
    {
        draggedPartSeparator = -1;
        return;
    }

    dragStartX = event.x;
    for (size_t part = 0; part < dragStartWidths.size(); ++part)
        dragStartWidths[part] = parts[part].getWidth();
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void LvlsView::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedPartSeparator < 0)
        return;

    const auto minimumPartWidths = getMinimumPartWidths();
    auto widths = dragStartWidths;
    const auto left = static_cast<size_t>(draggedLeftPart);
    const auto right = static_cast<size_t>(draggedRightPart);
    const auto pairWidth = dragStartWidths[left] + dragStartWidths[right];
    const auto minimumLeft = std::min(minimumPartWidths[left], pairWidth);
    const auto maximumLeft = std::max(minimumLeft, pairWidth - minimumPartWidths[right]);
    widths[left] = juce::jlimit(minimumLeft, maximumLeft,
                                dragStartWidths[left] + event.x - dragStartX);
    widths[right] = pairWidth - widths[left];

    const auto visibleParts = getVisibleParts();
    for (size_t index = 0; index < partWeights.size(); ++index)
        if (visibleParts[index])
            partWeights[index] = static_cast<float>(widths[index]);
    resized();
}

void LvlsView::mouseUp(const juce::MouseEvent&)
{
    if (draggedPartSeparator >= 0)
        processor.setLvlsSectionWeights(partWeights);

    draggedLeftPart = -1;
    draggedRightPart = -1;
    draggedPartSeparator = -1;
    setMouseCursor(hoveredPartSeparator >= 0
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

void LvlsView::timerCallback()
{
    if (draggedPartSeparator < 0)
    {
        const auto storedWeights = processor.getLvlsSectionWeights();
        const auto localTotal = std::max(0.001f,
            partWeights[0] + partWeights[1] + partWeights[2]);
        auto weightsChanged = false;
        for (size_t index = 0; index < partWeights.size(); ++index)
            weightsChanged = weightsChanged
                || std::abs(partWeights[index] / localTotal - storedWeights[index]) > 1.0e-4f;

        if (weightsChanged)
        {
            partWeights = storedWeights;
            resized();
        }
    }

   #if ANA_VARIANT_RTM
    auto* displayedLvls = &processor.getLvlsProcessor();
    const auto displayRevision = displayedLvls->getRevision();
   #else
    if (processor.getAnalyzerPageState() != ana::AnalyzerPage::lvls)
        return;

    processor.requestAraAnalysis(size_t { 1 });
    const auto analysis = processor.getAraAnalysisResult();
    if (analysis == nullptr || analysis->lvls == nullptr)
        return;

    auto* displayedLvls = analysis->lvls.get();
    const auto displayRevision = analysis->revision;
   #endif
    auto& lastRevision = displayedRevision;
    if (displayRevision == lastRevision)
        return;

    lastRevision = displayRevision;
    const auto values = displayedLvls->getValues();
    peakValues = values.peakDecibels;
    rmsValues = values.rmsDecibels;
    peakMaximumValues = values.peakMaximumDecibels;
    peakHoldValues = values.peakHoldDecibels;
    rmsMaximumValues = values.rmsMaximumDecibels;
    momentaryMaximumLufs = values.momentaryMaximumLufs;
    shortTermMaximumLufs = values.shortTermMaximumLufs;
    integratedMaximumLufs = values.integratedMaximumLufs;
    loudnessRange = values.loudnessRange;
    momentaryLufs = values.momentaryLufs;
    shortTermLufs = values.shortTermLufs;
    integratedLufs = values.integratedLufs;
    for (size_t series = 0; series < loudnessHistories.size(); ++series)
        displayedLvls->copyHistory(series, loudnessHistories[series]);
    repaint();
}

juce::Rectangle<float> LvlsView::getPlotBounds() const noexcept
{
    return getLocalBounds().toFloat();
}
