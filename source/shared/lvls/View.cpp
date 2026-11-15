#include "View.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/scop/Layout.h"
#include "shared/lvls/Settings.h"
#include "shared/lvls/Processor.h"
#include "shell/Processor.h"
#include "shared/shell/Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>

using ana::ui::analyzer_detail::readParameterValue;

namespace
{
int lvlsScaleLabelWidth() noexcept { return ana::ui::textControlWidth(3); }
int lvlsReadoutWidth() noexcept { return ana::ui::textControlWidth(7); }
int metricReadoutWidth() noexcept { return ana::ui::textControlWidth(7); }
int metricLabelWidth(const char* label) noexcept { return ana::ui::textControlWidth(label); }
constexpr std::array<const char*, 6> metricNames { "TP", "LRA", "PLRD", "RLRD", "PMSD", "RMSD" };
int metricsMinimumWidth() noexcept
{
    const auto cellWidth = metricLabelWidth("PLRD") + ana::ui::gap.pixels()
        + metricReadoutWidth();
    return cellWidth * 2 + ana::ui::gap.pixels();
}

struct LvlsMetricCell
{
    juce::Rectangle<int> label;
    juce::Rectangle<int> readout;
};

struct LvlsMetricsLayout
{
    juce::Rectangle<int> header;
    std::array<LvlsMetricCell, metricNames.size()> cells {};
};

LvlsMetricsLayout makeLvlsMetricsLayout(juce::Rectangle<int> bounds,juce::Rectangle<int> headerBounds) noexcept
{
    LvlsMetricsLayout layout;
    if (bounds.isEmpty())
        return layout;
    layout.header = bounds.removeFromTop(std::min(ana::ui::controlHeight, bounds.getHeight())).getIntersection(headerBounds);
    ana::ui::gap.removeFromTop(bounds);
    const auto labelWidth = metricLabelWidth("PLRD");
    const auto readoutWidth = metricReadoutWidth();
    const auto cellWidth = std::max(0,std::min(labelWidth+ana::ui::gap.pixels()+readoutWidth,
        (layout.header.getWidth()-ana::ui::gap.pixels())/2));
    const auto gridWidth=2*cellWidth+ana::ui::gap.pixels();
    bounds.setX(layout.header.getX()+(layout.header.getWidth()-gridWidth)/2);
    bounds.setWidth(gridWidth);
    const auto gap = ana::ui::gap.pixels();
    // Fixed two-column layout: TP/LRA, PLRD/RLRD, PMSD/RMSD.
    constexpr int columns = 2;
    const auto rowCount = static_cast<int>((layout.cells.size() + columns - 1) / columns);
    const auto rowGap = rowCount > 1 ? std::min(gap, std::max(0,
        (bounds.getHeight() - rowCount * ana::ui::controlHeight) / (rowCount - 1))) : 0;
    for (size_t index = 0; index < layout.cells.size(); ++index)
    {
        const auto rowIndex = static_cast<int>(index) / columns;
        const auto columnIndex = static_cast<int>(index) % columns;
        auto cellBounds = juce::Rectangle<int> {
            bounds.getX() + columnIndex * (cellWidth + gap),
            bounds.getY() + rowIndex * (ana::ui::controlHeight + rowGap),
            std::max(0, std::min(cellWidth, bounds.getWidth() - columnIndex * (cellWidth + gap))),
            std::max(0, std::min(ana::ui::controlHeight,
                     bounds.getHeight() - rowIndex * (ana::ui::controlHeight + rowGap)))
        };
        if (cellBounds.getWidth() <= 0 || cellBounds.getHeight() <= 0)
            continue;
        layout.cells[index].label = cellBounds.removeFromLeft(std::min(labelWidth, cellBounds.getWidth()));
        ana::ui::gap.removeFromLeft(cellBounds);
        layout.cells[index].readout = cellBounds.removeFromLeft(std::min(readoutWidth, cellBounds.getWidth()));
    }
    return layout;
}

juce::Rectangle<int> reserveHeaderButtons(const juce::Component& view,juce::Rectangle<int> header)
{
    const int left=header.getY()<ana::ui::analyzer_detail::topLeftOverlayBottom(view)?ana::ui::analyzer_detail::topLeftOverlayWidth(view):0;
    const int rightReserve=header.getY()<ana::ui::controlHeight?ana::ui::analyzer_detail::topRightOverlayWidth(view):0;
    const int right=view.getWidth()-(rightReserve>0?rightReserve+ana::scop::bandZoomSliderWidth+ana::ui::gap.pixels():0);
    return header.getIntersection(juce::Rectangle<int>(left,header.getY(),std::max(0,right-left),header.getHeight()));
}

// Signed level differences in dB. The floor (-120 dB) represents silence,
// so it must not be treated as a finite measurement when subtracting levels.
juce::String formatLevelDifference(const float first, const float second)
{
    constexpr auto silence = ana::lvls::LvlsProcessor::minimumDecibels + 0.05f;
    if (first <= silence && second <= silence) return "---";
    if (first <= silence) return "-inf";
    if (second <= silence) return "+inf";
    const auto difference = first - second;
    return juce::String::formatted("%+07.2f", std::abs(difference) < 0.005f ? 0.0f : difference);
}

using ana::ui::analyzer_detail::formatReadoutLevel;

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
    setName("lvls-display");
    partWeights = processor.getLvlsSectionWeights();
    refreshPersistentViewState();
    peakModeButton.onClick = [this]
    {
        showPeakLvls = true;
        setPersistentParameter(PluginProcessor::lvlsPeakModeParameterId, 1.0f);
        peakModeButton.setToggleState(true, juce::dontSendNotification);
        rmsModeButton.setToggleState(false, juce::dontSendNotification);
        repaint();
    };
    rmsModeButton.onClick = [this]
    {
        showPeakLvls = false;
        setPersistentParameter(PluginProcessor::lvlsPeakModeParameterId, 0.0f);
        peakModeButton.setToggleState(false, juce::dontSendNotification);
        rmsModeButton.setToggleState(true, juce::dontSendNotification);
        repaint();
    };
    peakChannelModeButton.onClick = [this]
    {
        showMidSideLvls = ! showMidSideLvls;
        setPersistentParameter(PluginProcessor::lvlsMidSideModeParameterId,
                               showMidSideLvls ? 1.0f : 0.0f);
        peakChannelModeButton.setToggleState(showMidSideLvls, juce::dontSendNotification);
        repaint();
    };
    peakModeButton.setToggleState(showPeakLvls, juce::dontSendNotification);
    rmsModeButton.setToggleState(! showPeakLvls, juce::dontSendNotification);
    peakChannelModeButton.setToggleState(showMidSideLvls, juce::dontSendNotification);
    addAndMakeVisible(peakModeButton);
    addAndMakeVisible(rmsModeButton);
    addAndMakeVisible(peakChannelModeButton);
    startTimerHz(30);
}

void LvlsView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::windowBackground);

    const auto minimumLvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth());
    const auto minimumLoudnessColumnWidth = std::max(getLvlsWidth(true) + 2, lvlsReadoutWidth());
    const auto parts = getPartBounds();
    const auto visibleParts = getVisibleParts();
    auto peakRmsBounds = parts[0];
    auto lufsBounds = parts[1];
    const auto metricsBounds = parts[2];
    const auto scaleSideWidth = lvlsScaleLabelWidth() + ana::ui::gap.pixels();
    const auto peakScalesVisible = peakRmsBounds.getWidth()
        >= minimumLvlsColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto peakLvlsHorizontalBounds = peakRmsBounds;
    if (peakScalesVisible)
        peakLvlsHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto peakLvlsWidth = std::max(1,
        (peakLvlsHorizontalBounds.getWidth() - ana::ui::gap.pixels()) / 2);
    const auto lufsScalesVisible = lufsBounds.getWidth()
        >= minimumLoudnessColumnWidth * 3 + ana::ui::gap.pixels() * 2 + scaleSideWidth * 2;
    auto lufsLvlsHorizontalBounds = lufsBounds;
    if (lufsScalesVisible)
        lufsLvlsHorizontalBounds.reduce(scaleSideWidth, 0);
    const auto lufsLvlsWidth = std::max(1,
        (lufsLvlsHorizontalBounds.getWidth() - ana::ui::gap.pixels() * 2) / 3);
    const auto peakChannelOffset = showMidSideLvls ? size_t { 2 } : size_t { 0 };

    const auto displayFont = ana::ui::makeFont();
    const auto peakLow = readParameterValue(processor, PluginProcessor::lvlsPeakRangeLowParameterId, ana::lvls::defaultDisplayLowDecibels);
    const auto peakHigh = std::max(peakLow + 0.1f,
                                   readParameterValue(processor, PluginProcessor::lvlsPeakRangeHighParameterId, ana::lvls::defaultDisplayHighDecibels));
    const auto lufsLow = readParameterValue(processor, PluginProcessor::lvlsLoudnessRangeLowParameterId, ana::lvls::defaultDisplayLowDecibels);
    const auto lufsHigh = std::max(lufsLow + 0.1f,
                                   readParameterValue(processor, PluginProcessor::lvlsLoudnessRangeHighParameterId, ana::lvls::defaultDisplayHighDecibels));
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
            graphics.setColour(ana::ui::light);
            if (drawLeftLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getX()) - ana::ui::gap.pixels() - lvlsScaleLabelWidth(),
                                  labelY, lvlsScaleLabelWidth(), ana::ui::controlHeight,
                                  juce::Justification::centredRight, true);
            if (drawRightLabels)
                graphics.drawText(label,
                                  juce::roundToInt(bounds.getRight()) + ana::ui::gap.pixels(),
                                  labelY, lvlsScaleLabelWidth(), ana::ui::controlHeight,
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
        graphics.setColour(ana::ui::dark);
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
            graphics.setColour(ana::ui::dark); graphics.fillRect(readoutBounds);
            graphics.setColour(ana::ui::light); graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            const auto readoutValue = showPeakLvls
                ? readoutPeakValues.peakDecibels[channel] : readoutPeakValues.rmsDecibels[channel];
            graphics.drawText(readoutValue <= -119.95f ? juce::String("-inf")
                                                     : formatReadoutLevel(readoutValue), ana::ui::readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };
    const auto drawLufsLvls = [&] (juce::Rectangle<int> bounds, const juce::String& title,
                                    const float value, const float readoutValue, const float maximum,
                                    const bool drawLeftScale,
                                    const bool drawRightScale)
    {
        auto titleBounds = reserveHeaderButtons(*this,bounds.removeFromTop(ana::ui::controlHeight));
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::light);
        graphics.setColour(ana::ui::dark); graphics.fillRect(titleBounds);
        graphics.setColour(ana::ui::light); graphics.drawRect(titleBounds, 1);
        graphics.setColour(ana::ui::white);
        graphics.drawText(title, titleBounds, juce::Justification::centred, true);
        ana::ui::gap.removeFromTop(bounds);
        auto maximumBounds = bounds.removeFromTop(ana::ui::controlHeight);
        graphics.setColour(ana::ui::light);
        graphics.setColour(ana::ui::dark); graphics.fillRect(maximumBounds);
        graphics.setColour(ana::ui::light); graphics.drawRect(maximumBounds, 1.0f);
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
        graphics.setColour(ana::ui::dark);
        graphics.fillRect(bar);
        const auto top = bar.getBottom() - normalise(value, lufsLow, lufsHigh) * bar.getHeight();
        graphics.setColour(ana::ui::light);
        graphics.fillRect(bar.getX(), top, bar.getWidth(), bar.getBottom() - top);
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
            graphics.setColour(ana::ui::dark); graphics.fillRect(readoutBounds);
            graphics.setColour(ana::ui::light); graphics.drawRect(readoutBounds, 1.0f);
            graphics.setFont(displayFont);
            graphics.setColour(ana::ui::white);
            graphics.drawText(readoutValue <= -119.95f ? juce::String("-inf")
                                                : formatLufsReadout(readoutValue), ana::ui::readoutTextBounds(readoutBounds),
                              juce::Justification::centred, true);
        }
    };

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
            graphics.setColour(ana::ui::dark); graphics.fillRect(labelBounds);
            graphics.setColour(ana::ui::light); graphics.drawRect(labelBounds, 1.0f);
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
            graphics.setColour(ana::ui::dark); graphics.fillRect(maximumBounds);
            graphics.setColour(ana::ui::light); graphics.drawRect(maximumBounds, 1.0f);
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
        auto loudnessHeader = reserveHeaderButtons(*this,lufsLvlsHorizontalBounds.removeFromTop(ana::ui::controlHeight));
        graphics.setColour(ana::ui::light);
        graphics.setColour(ana::ui::dark); graphics.fillRect(loudnessHeader);
        graphics.setColour(ana::ui::light); graphics.drawRect(loudnessHeader, 1.0f);
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::white);
        graphics.drawText("LOUDNESS", loudnessHeader, juce::Justification::centred, true);
        lufsBounds.removeFromTop(ana::ui::controlHeight);
        ana::ui::gap.removeFromTop(lufsBounds);
        lufsBounds.setX(lufsLvlsHorizontalBounds.getX());
        lufsBounds.setWidth(lufsGroupWidth);
        ana::ui::FixedGapRow lufsRow(lufsBounds);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "M", momentaryLufs,
                      readoutLoudnessValues.momentaryLufs, momentaryMaximumLufs, lufsScalesVisible, false);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "S", shortTermLufs,
                      readoutLoudnessValues.shortTermLufs, shortTermMaximumLufs, false, false);
        drawLufsLvls(lufsRow.takeLeft(lufsLvlsWidth), "I", integratedLufs,
                      readoutLoudnessValues.integratedLufs, integratedMaximumLufs, false, lufsScalesVisible);
    }

    if (visibleParts[2] && ! metricsBounds.isEmpty())
    {
        const auto metrics = makeLvlsMetricsLayout(metricsBounds,reserveHeaderButtons(*this,metricsBounds.withHeight(ana::ui::controlHeight)));
        graphics.setFont(displayFont);
        graphics.setColour(ana::ui::light);
        graphics.setColour(ana::ui::dark); graphics.fillRect(metrics.header);
        graphics.setColour(ana::ui::light); graphics.drawRect(metrics.header, 1.0f);
        graphics.setColour(ana::ui::white);
        graphics.drawText("OTHERS", metrics.header, juce::Justification::centred, true);
        const std::array<juce::String, 6> readouts {
            metricsTruePeak <= -119.95f ? juce::String("-inf")
                : formatReadoutLevel(metricsTruePeak),
            formatReadoutLevel(loudnessRange),
            metricsDifferenceReadouts[0], metricsDifferenceReadouts[2],
            metricsDifferenceReadouts[1], metricsDifferenceReadouts[3]
        };
        for (size_t index = 0; index < metrics.cells.size(); ++index)
        {
            const auto& cell = metrics.cells[index];
            if (cell.label.isEmpty() || cell.readout.isEmpty())
                continue;
            graphics.setColour(ana::ui::light);
            graphics.setColour(ana::ui::dark); graphics.fillRect(cell.label);
            graphics.setColour(ana::ui::light); graphics.drawRect(cell.label, 1.0f);
            graphics.setColour(ana::ui::dark); graphics.fillRect(cell.readout);
            graphics.setColour(ana::ui::light); graphics.drawRect(cell.readout, 1.0f);
            graphics.setColour(ana::ui::white);
            graphics.drawText(metricNames[index], cell.label, juce::Justification::centred, true);
            graphics.drawText(readouts[index], ana::ui::readoutTextBounds(cell.readout),
                              juce::Justification::centred, true);
        }
    }

}

void LvlsView::resized()
{
    layoutPeakModeButtons();
    repaint();
}

void LvlsView::refreshPartLayout()
{
    partWeights=processor.getLvlsSectionWeights();
    draggedPartSeparator=draggedLeftPart=draggedRightPart=hoveredPartSeparator=-1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    resized();
}

void LvlsView::layoutPeakModeButtons()
{
    const auto showPeakControls = getVisibleParts()[0];
    peakModeButton.setVisible(showPeakControls);
    rmsModeButton.setVisible(showPeakControls);
    peakChannelModeButton.setVisible(showPeakControls);
    if (! showPeakControls)
        return;

    const auto partBounds = getPartBounds()[0];
    const auto minimumLvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth());
    const auto scaleSideWidth = lvlsScaleLabelWidth() + ana::ui::gap.pixels();
    const auto scalesVisible = partBounds.getWidth()
        >= minimumLvlsColumnWidth * 2 + ana::ui::gap.pixels() + scaleSideWidth * 2;
    auto headerBounds = partBounds;
    if (scalesVisible)
        headerBounds.reduce(scaleSideWidth, 0);
    headerBounds = headerBounds.removeFromTop(ana::ui::controlHeight);
    const int leftReserve=ana::ui::analyzer_detail::topLeftOverlayWidth(*this);
    if(headerBounds.getX()<leftReserve) headerBounds.removeFromLeft(leftReserve-headerBounds.getX());
    const int rightReserve=ana::ui::analyzer_detail::topRightOverlayWidth(*this);
    if(rightReserve>0 && partBounds.getRight()==getWidth())
    {
        const int right=getWidth()-rightReserve-ana::scop::bandZoomSliderWidth-ana::ui::gap.pixels();
        if(headerBounds.getRight()>right) headerBounds.removeFromRight(headerBounds.getRight()-right);
    }
    const auto availableButtonWidth = std::max(0,
        headerBounds.getWidth() - ana::ui::gap.pixels() * 2);
    const auto preferredWidth = peakModeButton.getPreferredWidth()
        + rmsModeButton.getPreferredWidth() + peakChannelModeButton.getPreferredWidth();
    const auto peakPreferredWidth = peakModeButton.getPreferredWidth();
    const auto rmsPreferredWidth = rmsModeButton.getPreferredWidth();
    const auto extraPerButton = std::max(0, availableButtonWidth - preferredWidth) / 3;
    const auto peakWidth = availableButtonWidth < preferredWidth
        ? (availableButtonWidth * peakPreferredWidth + preferredWidth / 2) / preferredWidth
        : peakPreferredWidth + extraPerButton;
    const auto rmsWidth = availableButtonWidth < preferredWidth
        ? ((availableButtonWidth - peakWidth) * rmsPreferredWidth
           + (preferredWidth - peakPreferredWidth) / 2)
            / (preferredWidth - peakPreferredWidth)
        : rmsPreferredWidth + extraPerButton;
    ana::ui::FixedGapRow buttonRow(headerBounds);
    peakModeButton.setBounds(buttonRow.takeLeft(peakWidth));
    rmsModeButton.setBounds(buttonRow.takeLeft(rmsWidth));
    peakChannelModeButton.setBounds(buttonRow.remaining());
}

int LvlsView::getLvlsWidth(const bool loudness) const noexcept
{
    const auto* parameter = processor.getRawParameterValue(
        loudness ? PluginProcessor::lvlsLoudnessWidthParameterId : PluginProcessor::lvlsPeakRmsWidthParameterId);
    return juce::jlimit(ana::lvls::minimumMeterWidth, ana::lvls::maximumMeterWidth,
                        juce::roundToInt(parameter != nullptr
                            ? parameter->load(std::memory_order_relaxed)
                            : static_cast<float>(ana::lvls::defaultMeterWidth)));
}

std::array<bool, 3> LvlsView::getVisibleParts() const noexcept
{
    const auto enabled = [this] (const char* parameterId)
    {
        const auto* value = processor.getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    return {
        enabled(PluginProcessor::lvlsPeakRmsVisibleParameterId),
        enabled(PluginProcessor::lvlsLoudnessVisibleParameterId),
        enabled(PluginProcessor::lvlsMetricsVisibleParameterId)
    };
}

std::array<int, 3> LvlsView::getMinimumPartWidths() const noexcept
{
    const auto lvlsColumnWidth = std::max(getLvlsWidth() + 2, lvlsReadoutWidth());
    return {
        lvlsColumnWidth * 2 + ana::ui::gap.pixels(),
        std::max(getLvlsWidth(true) + 2, lvlsReadoutWidth()) * 3 + ana::ui::gap.pixels() * 2,
        metricsMinimumWidth()
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
        // Do not shrink OTHERS below its two-column minimum.  When the user
        // makes the meters wider than the editor can fit, compress the meter
        // sections instead; OTHERS must never reflow into six rows.
        const auto pinnedOthers = visible[2] ? std::min(usableWidth, minimumWidths[2]) : 0;
        widths[2] = pinnedOthers;
        const auto meterSpace = std::max(0, usableWidth - pinnedOthers);
        const auto meterMinimum = (visible[0] ? minimumWidths[0] : 0)
            + (visible[1] ? minimumWidths[1] : 0);
        if (meterMinimum > 0)
        {
            widths[0] = visible[0] ? juce::roundToInt(
                static_cast<double>(meterSpace) * minimumWidths[0] / meterMinimum) : 0;
            widths[1] = visible[1] ? meterSpace - widths[0] : 0;
        }
        else if (! visible[2])
            widths[0] = usableWidth;
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
    if (processor.areLvlsPartsCentered())
        return -1;

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
        if (! processor.isOfflineMode())
        {
        const auto section = parts[0].contains(event.getPosition()) ? 0
            : parts[1].contains(event.getPosition()) ? 1
            : parts[2].contains(event.getPosition()) ? 2 : -1;
        if (section >= 0 && ! processor.getLvlsProcessor().isFrozen())
            if (const auto* resetClick=processor.getRawParameterValue(PluginProcessor::lvlsResetClickParameterId);
                resetClick!=nullptr && resetClick->load(std::memory_order_relaxed)>=0.5f)
                processor.clearLvlsProcessor();
        }

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
    if (processor.areLvlsPartsCentered()) { refreshPartLayout(); return; }
    if (draggedPartSeparator < 0)
        return;

    const auto minimumPartWidths = getMinimumPartWidths();
    auto widths = dragStartWidths;
    const auto left = static_cast<size_t>(draggedLeftPart);
    const auto right = static_cast<size_t>(draggedRightPart);
    const auto pairWidth = dragStartWidths[left] + dragStartWidths[right];
    if (pairWidth < minimumPartWidths[left] + minimumPartWidths[right])
        return; // no adjustable slack; do not allow a transient under-minimum OTHERS
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
    if (!isShowing()) return;
    refreshPersistentViewState();
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

    auto* displayedLvls = &processor.getLvlsProcessor();
    auto* displayedLoudness = &processor.getLvlsLoudnessProcessor();
    auto* displayedMetrics = &processor.getLvlsMetricsProcessor();
    auto displayRevision = displayedLvls->getRevision()
        + displayedLoudness->getRevision() + displayedMetrics->getRevision();
    std::shared_ptr<const ana::offline::AnalysisResult> analysis;
    if (processor.isOfflineMode())
    {
        if (processor.getAnalyzerPageState() != ana::AnalyzerPage::lvls)
            return;
        processor.requestOfflineAnalysis(size_t { 1 });
        analysis = processor.getAnalysisResult();
        if (analysis == nullptr || analysis->lvls == nullptr)
            return;
        displayedLvls = analysis->lvls.get();
        displayedLoudness = displayedLvls;
        displayedMetrics = displayedLvls;
        displayRevision = analysis->revision;
    }
    const auto offlineMode = processor.isOfflineMode();
    const auto modeChanged = displayedOffline != offlineMode;
    if (modeChanged)
    {
        displayedOffline = offlineMode;
        readoutWindowStarted = false;
        displayedRevision = std::numeric_limits<uint64_t>::max();
    }

    const auto intervalMs = juce::jlimit(0, 3000, juce::roundToInt(readParameterValue(
        processor, PluginProcessor::lvlsReadoutIntervalMsParameterId,
        ana::lvls::defaultReadoutIntervalMilliseconds)));
    const auto now = static_cast<uint32_t>(juce::Time::getMillisecondCounter());
    const std::array<uint64_t, 3> resetEpochs {
        displayedLvls->getResetEpoch(), displayedLoudness->getResetEpoch(),
        displayedMetrics->getResetEpoch()
    };
    const auto intervalChanged = intervalMs != lastReadoutIntervalMs;
    const auto resetChanged = resetEpochs != lastReadoutResetEpochs;
    const auto windowDue = ! offlineMode && intervalMs > 0 && readoutWindowStarted
        && static_cast<uint32_t>(now - readoutWindowStartMs) >= static_cast<uint32_t>(intervalMs);
    if (displayRevision == displayedRevision && ! intervalChanged && ! resetChanged && ! windowDue)
        return;

    const auto newAudio = displayRevision != displayedRevision;
    displayedRevision = displayRevision;
    lastReadoutResetEpochs = resetEpochs;
    lastReadoutIntervalMs = intervalMs;

    const auto peak = displayedLvls->getValues();
    const auto values = displayedLoudness->getValues();
    const auto metrics = displayedMetrics->getValues();

    // Audio processing and meter ballistics are unaffected by the readout hold.
    peakValues = peak.peakDecibels;
    rmsValues = peak.rmsDecibels;
    peakHoldValues = peak.peakHoldDecibels;
    momentaryLufs = values.momentaryLufs;
    shortTermLufs = values.shortTermLufs;
    integratedLufs = values.integratedLufs;

    if (offlineMode)
    {
        // Offline numbers remain immediate and retain the existing maxima semantics.
        readoutPeakValues = peak;
        readoutLoudnessValues = values;
        readoutMetricsValues = metrics;
        readoutWindowStarted = false;
    }
    else
    {
        // The audio thread publishes the true peak of *every* block (including
        // between GUI timer callbacks). Consume each batch into this interval.
        const auto newPeaks = displayedLvls->consumeReadoutPeakGains();
        const auto newMetricPeaks = displayedMetrics->consumeReadoutPeakGains();
        for (size_t channel = 0; channel < pendingPeakReadoutGains.size(); ++channel)
        {
            pendingPeakReadoutGains[channel] = std::max(pendingPeakReadoutGains[channel], newPeaks[channel]);
            pendingMetricsReadoutGains[channel] = std::max(pendingMetricsReadoutGains[channel], newMetricPeaks[channel]);
        }
        if (intervalMs == 0)
        {
            // LIVE mode preserves the original continuously changing readouts.
            readoutPeakValues = peak;
            readoutLoudnessValues = values;
            readoutMetricsValues = metrics;
            readoutWindowStarted = false;
            pendingPeakReadoutGains.fill(0.0f);
            pendingMetricsReadoutGains.fill(0.0f);
            readoutWindowHasAudio = false;
        }
        else if (! readoutWindowStarted || intervalChanged || resetChanged)
        {
            // Show a useful initial value immediately, then sample-and-hold
            // complete windows at the selected cadence.
            readoutPeakValues = peak;
            readoutLoudnessValues = values;
            readoutMetricsValues = metrics;
            readoutWindowStartMs = now;
            readoutWindowStarted = true;
            readoutWindowHasAudio = newAudio;
            // The new window starts now. Discard any peak maxima left over
            // from before the panel opened or while another interval was set.
            pendingPeakReadoutGains.fill(0.0f);
            pendingMetricsReadoutGains.fill(0.0f);
        }
        else
        {
            readoutWindowHasAudio = readoutWindowHasAudio || newAudio;
            if (windowDue)
            {
                if (readoutWindowHasAudio)
                {
                    readoutPeakValues = peak;
                    readoutLoudnessValues = values;
                    readoutMetricsValues = metrics;
                    // PEAK is the true maximum over the whole elapsed interval,
                    // not a decaying meter value sampled at its endpoint.
                    for (size_t channel = 0; channel < pendingPeakReadoutGains.size(); ++channel)
                    {
                        readoutPeakValues.peakDecibels[channel] = juce::Decibels::gainToDecibels(
                            std::max(pendingPeakReadoutGains[channel], 1.0e-6f),
                            ana::lvls::LvlsProcessor::minimumDecibels);
                        readoutMetricsValues.peakDecibels[channel] = juce::Decibels::gainToDecibels(
                            std::max(pendingMetricsReadoutGains[channel], 1.0e-6f),
                            ana::lvls::LvlsProcessor::minimumDecibels);
                    }
                }
                pendingPeakReadoutGains.fill(0.0f);
                pendingMetricsReadoutGains.fill(0.0f);
                readoutWindowHasAudio = false;
                readoutWindowStartMs = now;
            }
        }
    }

    peakMaximumValues = readoutPeakValues.peakMaximumDecibels;
    rmsMaximumValues = readoutPeakValues.rmsMaximumDecibels;
    metricsTruePeak = std::max(readoutMetricsValues.peakMaximumDecibels[0],
                               readoutMetricsValues.peakMaximumDecibels[1]);
    const auto& metricPeaks = displayedOffline ? readoutMetricsValues.peakMaximumDecibels
                                                : readoutMetricsValues.peakDecibels;
    const auto& metricRms = displayedOffline ? readoutMetricsValues.rmsMaximumDecibels
                                              : readoutMetricsValues.rmsDecibels;
    metricsDifferenceReadouts = {
        formatLevelDifference(metricPeaks[0], metricPeaks[1]),
        formatLevelDifference(metricPeaks[2], metricPeaks[3]),
        formatLevelDifference(metricRms[0], metricRms[1]),
        formatLevelDifference(metricRms[2], metricRms[3])
    };
    momentaryMaximumLufs = readoutLoudnessValues.momentaryMaximumLufs;
    shortTermMaximumLufs = readoutLoudnessValues.shortTermMaximumLufs;
    integratedMaximumLufs = readoutLoudnessValues.integratedMaximumLufs;
    loudnessRange = readoutMetricsValues.loudnessRange;
    repaint();
}

void LvlsView::setPersistentParameter(const char* parameterId, const float value)
{
    auto* parameter = processor.getActiveParameter(parameterId);
    if (parameter == nullptr)
        return;

    const auto normalised = parameter->convertTo0to1(value);
    if (juce::approximatelyEqual(parameter->getValue(), normalised))
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalised);
    parameter->endChangeGesture();
}

void LvlsView::refreshPersistentViewState()
{
    const auto nextShowPeak = readParameterValue(processor, PluginProcessor::lvlsPeakModeParameterId, 1.0f) >= 0.5f;
    const auto nextShowMidSide = readParameterValue(processor, PluginProcessor::lvlsMidSideModeParameterId, 0.0f) >= 0.5f;
    const auto displayChanged = showPeakLvls != nextShowPeak || showMidSideLvls != nextShowMidSide;
    showPeakLvls = nextShowPeak;
    showMidSideLvls = nextShowMidSide;
    peakModeButton.setToggleState(showPeakLvls, juce::dontSendNotification);
    rmsModeButton.setToggleState(! showPeakLvls, juce::dontSendNotification);
    peakChannelModeButton.setToggleState(showMidSideLvls, juce::dontSendNotification);
    if (displayChanged)
        repaint();
}
