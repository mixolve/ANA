#include "View.h"
#include "Processor.h"
#include "../shell/Processor.h"
#include "shared/shell/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr int bandZoomValueWidth = ana::ui::textControlWidth(6);
constexpr int bandZoomSliderWidth = 14;
constexpr int minimumBandHeight = ana::ui::gap.pixels() * 2 + ana::ui::controlHeight;
constexpr int waveformRightInset = ana::ui::gap.pixels() + bandZoomSliderWidth;
constexpr std::array<const char*, ana::scopChannelModeCount> scopModeButtonNames { "LR", "L", "R", "MS", "M", "S" };
constexpr std::array<ana::ScopChannelMode, ana::scopChannelModeCount> scopModeButtonModes {
    ana::ScopChannelMode::lr,
    ana::ScopChannelMode::left,
    ana::ScopChannelMode::right,
    ana::ScopChannelMode::ms,
    ana::ScopChannelMode::mid,
    ana::ScopChannelMode::side
};

struct DisplayedModes
{
    std::array<size_t, 2> indices {};
    size_t count = 1;
};

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
    return (displayValue > 0.0f ? "+" : "") + juce::String(displayValue, 2);
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

ScopView::ScopView(PluginProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(false);
    bandHeightWeights.fill(1.0f);
    singleViewBand = processor.getScopSingleViewBand();
    fullSourceView = processor.isScopFullSourceView();

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto button = std::make_unique<ControlButton>(scopModeButtonNames[modeIndex]);
            button->onClick = [this, bandIndex, modeIndex]
            {
                const auto mode = scopModeButtonModes[modeIndex];

                if (processor.getScopChannelMode(bandIndex) != mode)
                {
                    processor.setScopChannelMode(bandIndex, mode);
                }

                refreshBandModeButtons();
            };
            addAndMakeVisible(*button);
            bandModeButtons[bandIndex][modeIndex] = std::move(button);
        }

        auto clearButton = std::make_unique<ControlButton>("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);

        auto singleViewButton = std::make_unique<ControlButton>("SVIEW");
        singleViewButton->onClick = [this, bandIndex]
        {
            if (fullSourceView)
            {
                fullSourceView = false;
                processor.setScopFullSourceView(false);
            }
            singleViewBand = singleViewBand == static_cast<int>(bandIndex)
                ? -1
                : static_cast<int>(bandIndex);
            processor.setScopSingleViewBand(singleViewBand);
            draggedBandSeparator = -1;
            refreshBandModeButtons();
            repaint();
        };
        addAndMakeVisible(*singleViewButton);
        bandSingleViewButtons[bandIndex] = std::move(singleViewButton);

        auto zoomSlider = std::make_unique<juce::Slider>();
        zoomSlider->setLookAndFeel(&bandZoomLookAndFeel);
        zoomSlider->setSliderStyle(juce::Slider::LinearBarVertical);
        zoomSlider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        zoomSlider->setRange(-48.0, 96.0, 0.1);
        zoomSlider->setValue(processor.getScopVerticalZoomDecibels(bandIndex), juce::dontSendNotification);
        zoomSlider->setSliderSnapsToMousePosition(false);
        zoomSlider->setScrollWheelEnabled(true);
        zoomSlider->setWantsKeyboardFocus(false);
        zoomSlider->setDoubleClickReturnValue(true, 0.0);
        zoomSlider->onValueChange = [this, bandIndex, slider = zoomSlider.get()]
        {
            if (! updatingBandControls)
            {
                processor.setScopVerticalZoomDecibels(bandIndex, static_cast<float>(slider->getValue()));
                refreshBandModeButtons();
                repaint();
            }
        };
        addAndMakeVisible(*zoomSlider);
        bandZoomSliders[bandIndex] = std::move(zoomSlider);

        auto zoomValueLabel = std::make_unique<EllipsisLabel>();
        zoomValueLabel->setFont(ana::ui::makeFont());
        zoomValueLabel->setJustificationType(juce::Justification::centred);
        zoomValueLabel->setColour(juce::Label::textColourId, ana::ui::white);
        zoomValueLabel->setColour(juce::Label::backgroundColourId, ana::ui::dark);
        zoomValueLabel->setColour(juce::Label::outlineColourId, ana::ui::light);
        zoomValueLabel->setBorderSize(juce::BorderSize<int>(1));
        zoomValueLabel->setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
        zoomValueLabel->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*zoomValueLabel);
        bandZoomValueLabels[bandIndex] = std::move(zoomValueLabel);

    }

    resetHistory();
    refreshBandModeButtons();
    startTimerHz(60);
}

void ScopView::setFrozen(const bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;

    if (! frozen)
    {
        history.readCursor = processor.getMultibandScop().getWriteCursor();
        history.columnSampleProgress = 0.0;
        history.resetColumnAccumulator();
    }
}

void ScopView::clearHistory()
{

    resetHistory();
}

void ScopView::refreshWaveform()
{

    repaint();
}

void ScopView::equalizeBandHeights()
{
    bandHeightWeights.fill(1.0f);
    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void ScopView::refreshDisplaySettings()
{
    resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void ScopView::setFullSourceView(const bool shouldShowFullSource)
{
    if (fullSourceView == shouldShowFullSource)
        return;

    fullSourceView = shouldShowFullSource;
    if (fullSourceView)
    {
        singleViewBand = -1;
        processor.setScopSingleViewBand(-1);
    }

    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void ScopView::timerCallback()
{
    const auto timeMilliseconds = processor.getScopTimeMilliseconds();
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    std::array<ana::ScopChannelMode, ana::MultibandScop::numBands> currentModes;

    if (fullSourceView != processor.isScopFullSourceView())
        setFullSourceView(processor.isScopFullSourceView());

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = processor.getScopChannelMode(bandIndex);

        if (history.channelModes[bandIndex] != currentModes[bandIndex])
        {
            history.channelModes[bandIndex] = currentModes[bandIndex];
        }
    }

    refreshBandModeButtons();

    const auto* leftToRightParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::scopLeftToRightParameterId);
    const auto leftToRight = leftToRightParameter != nullptr
        && leftToRightParameter->load(std::memory_order_relaxed) >= 0.5f;
    const auto directionState = leftToRight ? 1 : 0;
    if (renderedLeftToRight != directionState)
    {
        renderedLeftToRight = directionState;
        leftToRightColumn = 0;
        resetHistory();
        return;
    }

    if (frozen)
        return;

    if (juce::Time::getMillisecondCounterHiRes() < rtmResumeTimeMilliseconds)
    {
        history.readCursor = processor.getMultibandScop().getWriteCursor();
        history.columnSampleProgress = 0.0;
        history.resetColumnAccumulator();
        return;
    }

    if (std::abs(history.timeMilliseconds - timeMilliseconds) > 0.001
        || history.bandCount != activeBandCount)
    {
        resetHistory();
        return;
    }

    if (getWidth() <= 0 || history.bands.front().front().minimums.empty())
        return;

    auto& scop = processor.getMultibandScop();
    scop.copySince(incomingSamples, history.readCursor);

    const auto sampleCount = incomingSamples.front().front().size();
    if (sampleCount == 0)
        return;

    const auto visibleHistoryColumns = std::max<size_t>(
        1, history.bands.front().front().minimums.size());
    const auto samplesPerColumn = std::max(
        0.001,
        scop.getSampleRate() * timeMilliseconds * 0.001
            / static_cast<double>(visibleHistoryColumns));
    auto appendedColumn = false;

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        history.accumulateWideband(getRtmWidebandAnalysisChannelSamples(sampleIndex));

        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
            history.accumulateBand(bandIndex, getRtmAnalysisChannelSamples(bandIndex, sampleIndex));

        history.columnSampleProgress += 1.0;
        const auto columnsToAppend = static_cast<size_t>(history.columnSampleProgress / samplesPerColumn);

        if (columnsToAppend == 0)
            continue;

        for (size_t columnIndex = 0; columnIndex < columnsToAppend; ++columnIndex)
            appendHistoryColumn(activeBandCount);

        history.columnSampleProgress -= static_cast<double>(columnsToAppend) * samplesPerColumn;
        history.resetColumnAccumulator();
        appendedColumn = true;
    }

    if (appendedColumn)
        repaint();
}

void ScopView::paint(juce::Graphics& graphics)
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    const auto filledStyle = processor.isScopFilledStyle();
    graphics.setColour(ana::ui::opacityShade(processor.getScopOpacity()));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {

        const auto laneBounds = getBandBounds(bandIndex, activeBandCount);

        if (laneBounds.isEmpty())
            continue;

        auto waveformBounds = laneBounds;
        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);
        if (zoomSlidersVisible)
        {
            waveformBounds.setRight(std::max(
                waveformBounds.getX() + 1.0f,
                static_cast<float>(getWidth() - waveformRightInset)));
        }

        const auto displayedModes = getDisplayedModes(processor.getScopChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            auto displayBounds = waveformBounds;

            if (displayedModes.count == 2)
            {
                const auto halfHeight = waveformBounds.getHeight() * 0.5f;
                displayBounds.setY(waveformBounds.getY() + halfHeight * static_cast<float>(displayIndex));
                displayBounds.setHeight(displayIndex == 0
                    ? halfHeight
                    : waveformBounds.getBottom() - displayBounds.getY());
            }

            const auto modeIndex = displayedModes.indices[displayIndex];
            const auto* envelope = fullSourceView
                ? &history.wideband[modeIndex]
                : &history.bands[bandIndex][modeIndex];

            if (envelope != nullptr)
                drawWaveformEnvelope(graphics, envelope->minimums, envelope->maximums, displayBounds,
                                     0.0f, 1.0f,
                                     processor.getScopVerticalZoomDecibels(bandIndex),
                                     filledStyle);
        }
    }

    graphics.setColour(ana::ui::dark);

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        auto waveformBounds = getBandBounds(bandIndex, activeBandCount);

        if (waveformBounds.isEmpty())
            continue;

        const auto zoomSlidersVisible = shouldShowZoomSliders(bandIndex, activeBandCount);

        const auto lineWidth = zoomSlidersVisible
            ? std::max(0, getWidth() - waveformRightInset)
            : getWidth();
        const auto displayedModes = getDisplayedModes(processor.getScopChannelMode(bandIndex));

        for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
        {
            const auto centre = displayedModes.count == 1
                ? waveformBounds.getCentreY()
                : waveformBounds.getY()
                    + waveformBounds.getHeight() * (static_cast<float>(displayIndex) + 0.5f) * 0.5f;
            graphics.fillRect(0, juce::roundToInt(centre), lineWidth, 1);
        }

        if (displayedModes.count == 2)
        {
            graphics.setColour(ana::ui::white);
            graphics.fillRect(0, juce::roundToInt(waveformBounds.getCentreY()), lineWidth, 1);
            graphics.setColour(ana::ui::dark);
        }
    }

    graphics.setColour(ana::ui::white);

    for (size_t bandIndex = 1;
         ! fullSourceView && singleViewBand < 0 && bandIndex < activeBandCount;
         ++bandIndex)
    {
        const auto y = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());
        graphics.fillRect(0, y, getWidth(), 1);
    }
}

void ScopView::resized()
{
    const auto sizeChanged = lastComponentWidth != getWidth()
        || lastComponentHeight != getHeight();
    lastComponentWidth = getWidth();
    lastComponentHeight = getHeight();
    if (sizeChanged && history.containsRecordedData)
    {
        rtmResumeTimeMilliseconds = juce::Time::getMillisecondCounterHiRes() + 120.0;
        history.readCursor = processor.getMultibandScop().getWriteCursor();
        history.columnSampleProgress = 0.0;
        history.resetColumnAccumulator();
    }

    if (! history.containsRecordedData)
        resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void ScopView::resetHistory()
{

    std::array<ana::ScopChannelMode, ana::MultibandScop::numBands> modes {};
    for (size_t bandIndex = 0; bandIndex < modes.size(); ++bandIndex)
        modes[bandIndex] = processor.getScopChannelMode(bandIndex);

    history.reset(getWaveformColumnCount(),
                  processor.getScopTimeMilliseconds(),
                  processor.getCrossoverCount() + 1,
                  processor.getMultibandScop().getWriteCursor(),
                  modes);
    leftToRightColumn = 0;
    repaint();
}

void ScopView::resizeHistory(const size_t newColumnCount)
{
    history.resize(newColumnCount);
    repaint();
}

void ScopView::clearBandHistory(const size_t bandIndex)
{
    if (bandIndex >= history.bandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, history.bandCount).toNearestInt();
    if (fullSourceView)
    {
        history.clearWideband();
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    history.clearBand(bandIndex);
    refreshBandModeButtons();
    repaint(bandBounds);
}


void ScopView::refreshBandModeButtons()
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    const auto persistedSingleViewBand = fullSourceView ? -1 : processor.getScopSingleViewBand();

    if (persistedSingleViewBand != singleViewBand)
        singleViewBand = persistedSingleViewBand;

    if (singleViewBand >= static_cast<int>(activeBandCount))
    {
        singleViewBand = -1;
        processor.setScopSingleViewBand(-1);
    }

    constexpr int buttonHeight = ana::ui::controlHeight;
    constexpr int zoomSliderWidth = bandZoomSliderWidth;
    constexpr int zoomValueWidth = bandZoomValueWidth;
    const auto showZoomControls = processor.areScopZoomControlsVisible();
    const auto showMonitorControls = processor.areScopMonitorControlsVisible();
    const auto showTools = processor.areScopToolsVisible();
    const juce::ScopedValueSetter<bool> controlUpdate(updatingBandControls, true);

    for (size_t bandIndex = 0; bandIndex < bandModeButtons.size(); ++bandIndex)
    {
        const auto isActive = bandIndex < activeBandCount;
        const auto isVisibleBand = isActive
            && (fullSourceView
                ? bandIndex == 0
                : (singleViewBand < 0 || singleViewBand == static_cast<int>(bandIndex)));
        const auto selectedMode = processor.getScopChannelMode(bandIndex);
        const auto verticalZoomDecibels = processor.getScopVerticalZoomDecibels(bandIndex);
        const auto laneBounds = isVisibleBand
            ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
            : juce::Rectangle<int>();
        const auto showZoomSliders = isVisibleBand
            && shouldShowZoomSliders(bandIndex, activeBandCount);
        const auto laneTopInset = laneBounds.getY() == 0 ? 0 : ana::ui::gap.pixels();
        const auto controlsY = laneBounds.getY() + laneTopInset;
        const auto preferredZoomControlsWidth = showZoomControls
            ? zoomValueWidth + (showZoomSliders ? ana::ui::gap.pixels() + zoomSliderWidth : 0)
            : 0;
        const auto zoomControlsFit = getWidth() >= preferredZoomControlsWidth;
        const auto zoomControlsWidth = zoomControlsFit ? preferredZoomControlsWidth : 0;
        ana::ui::FixedGapRow controlsRow({ 0, controlsY, std::max(0, getWidth() - zoomControlsWidth), buttonHeight });

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isVisibleBand && showMonitorControls);
            button.setToggleState(selectedMode == scopModeButtonModes[modeIndex],
                                  juce::dontSendNotification);

            if (isVisibleBand && showMonitorControls)
                button.setBounds(controlsRow.takeLeft(button.getPreferredWidth()));
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isVisibleBand && showTools);
        if (isVisibleBand && showTools)
            clearButton.setBounds(controlsRow.takeLeft(clearButton.getPreferredWidth()));

        auto& singleViewButton = *bandSingleViewButtons[bandIndex];
        singleViewButton.setVisible(isVisibleBand && showTools && ! fullSourceView);
        singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                        juce::dontSendNotification);
        if (isVisibleBand && showTools)
            singleViewButton.setBounds(controlsRow.takeLeft(singleViewButton.getPreferredWidth()));

        auto& zoomSlider = *bandZoomSliders[bandIndex];
        auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
        zoomSlider.setVisible(showZoomSliders);
        zoomValueLabel.setVisible(isVisibleBand && showZoomControls && zoomControlsFit);
        zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
        zoomSlider.setTooltip("ZOOM " + formatZoomValue(verticalZoomDecibels));
        zoomValueLabel.setText(formatZoomValue(verticalZoomDecibels), juce::dontSendNotification);

        if (isVisibleBand && showZoomControls && zoomControlsFit)
        {
            const auto buttonY = controlsY;
            const auto sliderX = showZoomSliders
                ? std::max(0, getWidth() - zoomSliderWidth)
                : getWidth();
            const auto zoomValueX = sliderX - (showZoomSliders ? ana::ui::gap.pixels() : 0)
                - zoomValueWidth;
            zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);

            if (showZoomSliders)
            {
                zoomSlider.setBounds(sliderX, buttonY, zoomSliderWidth,
                                     std::max(1, laneBounds.getBottom() - buttonY));
            }
        }
    }
}

juce::Rectangle<float> ScopView::getBandBounds(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    if (activeBandCount == 0 || bandIndex >= activeBandCount)
        return {};

    if (fullSourceView)
    {
        if (bandIndex != 0)
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    if (singleViewBand >= 0)
    {
        if (singleViewBand != static_cast<int>(bandIndex))
            return {};

        return { 0.0f, 0.0f, static_cast<float>(std::max(0, getWidth() - 1)),
                 static_cast<float>(getHeight()) };
    }

    auto totalWeight = 0.0f;
    auto precedingWeight = 0.0f;

    for (size_t index = 0; index < activeBandCount; ++index)
    {
        const auto weight = std::max(0.001f, bandHeightWeights[index]);
        totalWeight += weight;

        if (index < bandIndex)
            precedingWeight += weight;
    }

    const auto availableHeight = static_cast<float>(getHeight());
    const auto fixedHeight = std::min(static_cast<float>(minimumBandHeight),
                                      availableHeight / static_cast<float>(activeBandCount));
    const auto distributableHeight = std::max(0.0f,
        availableHeight - fixedHeight * static_cast<float>(activeBandCount));
    const auto top = fixedHeight * static_cast<float>(bandIndex)
        + distributableHeight * precedingWeight / totalWeight;
    const auto bottomWeight = precedingWeight + std::max(0.001f, bandHeightWeights[bandIndex]);
    const auto bottom = bandIndex + 1 == activeBandCount
        ? availableHeight
        : fixedHeight * static_cast<float>(bandIndex + 1)
            + distributableHeight * bottomWeight / totalWeight;
    return { 0.0f, top, static_cast<float>(std::max(0, getWidth() - 1)),
             std::max(0.0f, bottom - top) };
}

bool ScopView::shouldShowZoomSliders(
    const size_t bandIndex, const size_t activeBandCount) const noexcept
{
    return processor.areScopZoomControlsVisible()
        && getBandBounds(bandIndex, activeBandCount).getHeight()
            >= static_cast<float>(minimumBandHeight);
}

bool ScopView::hasVisibleZoomSliders() const noexcept
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        if (shouldShowZoomSliders(bandIndex, activeBandCount))
            return true;

    return false;
}

size_t ScopView::getWaveformColumnCount() const noexcept
{
    const auto drawableWidth = hasVisibleZoomSliders()
        ? getWidth() - waveformRightInset
        : getWidth() - 1;
    return static_cast<size_t>(std::max(1, drawableWidth));
}

int ScopView::findBandSeparator(
    const int y, const size_t activeBandCount) const noexcept
{
    if (fullSourceView || singleViewBand >= 0)
        return -1;

    constexpr int hitRadius = 5;

    for (size_t bandIndex = 1; bandIndex < activeBandCount; ++bandIndex)
    {
        const auto separatorY = juce::roundToInt(getBandBounds(bandIndex, activeBandCount).getY());

        if (std::abs(y - separatorY) <= hitRadius)
            return static_cast<int>(bandIndex - 1);
    }

    return -1;
}

void ScopView::mouseMove(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    setMouseCursor(findBandSeparator(event.y, activeBandCount) >= 0
        ? juce::MouseCursor::UpDownResizeCursor
        : juce::MouseCursor::NormalCursor);
}

void ScopView::mouseExit(const juce::MouseEvent&)
{
    if (draggedBandSeparator < 0)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ScopView::mouseDown(const juce::MouseEvent& event)
{
    draggedBandSeparator = findBandSeparator(event.y, processor.getCrossoverCount() + 1);

    if (draggedBandSeparator >= 0)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ScopView::mouseDrag(const juce::MouseEvent& event)
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;

    if (draggedBandSeparator < 0
        || static_cast<size_t>(draggedBandSeparator + 1) >= activeBandCount)
        return;

    const auto upperIndex = static_cast<size_t>(draggedBandSeparator);
    const auto lowerIndex = upperIndex + 1;
    const auto upperBounds = getBandBounds(upperIndex, activeBandCount);
    const auto lowerBounds = getBandBounds(lowerIndex, activeBandCount);
    const auto combinedTop = upperBounds.getY();
    const auto combinedBottom = lowerBounds.getBottom();
    const auto combinedHeight = combinedBottom - combinedTop;

    if (combinedHeight <= 1.0f)
        return;

    const auto minimumHeight = std::min(static_cast<float>(minimumBandHeight),
                                        combinedHeight * 0.5f);
    const auto separatorY = juce::jlimit(combinedTop + minimumHeight,
                                         combinedBottom - minimumHeight,
                                         static_cast<float>(event.y));
    const auto distributableHeight = combinedHeight - minimumHeight * 2.0f;

    if (distributableHeight <= 0.0f)
        return;

    const auto upperRatio = (separatorY - combinedTop - minimumHeight) / distributableHeight;
    const auto combinedWeight = bandHeightWeights[upperIndex] + bandHeightWeights[lowerIndex];
    bandHeightWeights[upperIndex] = combinedWeight * upperRatio;
    bandHeightWeights[lowerIndex] = combinedWeight * (1.0f - upperRatio);
    refreshBandModeButtons();
    repaint();
}

void ScopView::mouseUp(const juce::MouseEvent& event)
{
    draggedBandSeparator = -1;
    mouseMove(event);
}

ana::scop::AnalysisChannelSamples
ScopView::getRtmAnalysisChannelSamples(const size_t bandIndex,
                                                   const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];
    return ana::scop::makeAnalysisChannelSamples(left, right);
}

ana::scop::AnalysisChannelSamples
ScopView::getRtmWidebandAnalysisChannelSamples(const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples.wideband[0][sampleIndex];
    const auto right = incomingSamples.wideband[1][sampleIndex];
    return ana::scop::makeAnalysisChannelSamples(left, right);
}

void ScopView::appendHistoryColumn(const size_t activeBandCount)
{
    const auto leftToRight = renderedLeftToRight == 1;
    if (! leftToRight)
    {
        history.appendColumn(activeBandCount);
        return;
    }

    const auto columnCount = history.bands.front().front().minimums.size();
    if (columnCount == 0)
        return;

    if (leftToRightColumn >= columnCount)
    {
        history.clearRecordedData();
        leftToRightColumn = 0;
    }

    history.writeColumn(activeBandCount, leftToRightColumn++);
}
