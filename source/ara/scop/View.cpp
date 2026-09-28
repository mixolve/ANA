#include "View.h"
#include "../shell/Processor.h"
#include "shared/shell/Theme.h"
#include "shared/scop/Layout.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace
{
float readParameterValue(const PluginProcessor& processor, const char* parameterId,
                         const float fallback) noexcept
{
    if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
        return value->load(std::memory_order_relaxed);
    return fallback;
}

void setParameterPlainValue(PluginProcessor& processor, const char* parameterId,
                            const float value)
{
    auto* parameter = processor.getParameters().getParameter(parameterId);
    if (parameter == nullptr)
        return;
    const auto normalised = parameter->convertTo0to1(value);
    if (juce::approximatelyEqual(parameter->getValue(), normalised))
        return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalised);
    parameter->endChangeGesture();
}

int bandZoomValueWidth() noexcept
{
    return ana::ui::textControlWidth(6);
}
juce::String formatTimeReadout(const double seconds)
{
    const auto safeSeconds = std::max(0.0, seconds);
    const auto minutes = static_cast<int>(safeSeconds / 60.0);
    return juce::String::formatted("%02d:%06.3f", minutes,
        safeSeconds - static_cast<double>(minutes) * 60.0);
}
int bandTimeReadoutWidth() noexcept
{
    return ana::ui::textControlWidth(9);
}
constexpr int bandRangeSliderHeight = ana::scop::bandRangeSliderHeight;
constexpr int bandZoomSliderWidth = bandRangeSliderHeight;
constexpr int minimumBandHeight = ana::scop::minimumAraBandHeight;
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

ScopView::ScopView(PluginProcessor& processorRef)
    : processor(processorRef)
{
    setOpaque(false);
    bandHeightWeights = processor.getScopBandHeightWeights();
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
                    processor.setScopBandNormalized(bandIndex, false);
                }

                refreshBandModeButtons();
            };
            addAndMakeVisible(*button);
            bandModeButtons[bandIndex][modeIndex] = std::move(button);
        }

        auto clearButton = std::make_unique<ControlButton>("eraser");
        clearButton->setTooltip("CLEAR");
        clearButton->onClick = [this, bandIndex] { clearBandHistory(bandIndex); };
        addAndMakeVisible(*clearButton);
        bandClearButtons[bandIndex] = std::move(clearButton);

        auto singleViewButton = std::make_unique<ControlButton>("browser-maximize");
        singleViewButton->setTooltip("SVIEW");
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
                processor.setScopBandNormalized(bandIndex, false);
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

        const auto makeTimeLabel = [this]
        {
            auto label = std::make_unique<EllipsisLabel>();
            label->setFont(ana::ui::makeFont());
            label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId, ana::ui::white);
            label->setColour(juce::Label::backgroundColourId, ana::ui::dark);
            label->setColour(juce::Label::outlineColourId, ana::ui::light);
            label->setBorderSize(juce::BorderSize<int>(1));
            label->setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
            label->setInterceptsMouseClicks(false, false);
            addAndMakeVisible(*label);
            return label;
        };
        bandTimeStartLabels[bandIndex] = makeTimeLabel();
        bandTimeEndLabels[bandIndex] = makeTimeLabel();

        auto normalizeButton = std::make_unique<ControlButton>("N");
        normalizeButton->onClick = [this, bandIndex] { normalizeBandWithZoom(bandIndex); };
        addAndMakeVisible(*normalizeButton);
        bandNormalizeButtons[bandIndex] = std::move(normalizeButton);

        auto rangeSlider = std::make_unique<RangeSlider>();
        rangeSlider->setRange(
            readParameterValue(processor, PluginProcessor::scopRangeStartParameterIds[bandIndex], 0.0f),
            readParameterValue(processor, PluginProcessor::scopRangeEndParameterIds[bandIndex], 1.0f));
        rangeSlider->onRangeChanged = [this, bandIndex]
        {
            if (! updatingBandControls)
            {
                setParameterPlainValue(processor, PluginProcessor::scopRangeStartParameterIds[bandIndex],
                                       bandRangeSliders[bandIndex]->getRangeStart());
                setParameterPlainValue(processor, PluginProcessor::scopRangeEndParameterIds[bandIndex],
                                       bandRangeSliders[bandIndex]->getRangeEnd());
            }
            repaint();
        };
        addAndMakeVisible(*rangeSlider);
        bandRangeSliders[bandIndex] = std::move(rangeSlider);

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

}

void ScopView::clearHistory()
{
    if (showingAraAnalysis)
    {
        displayedAraAnalysis.reset();
        clearedBands.fill(true);
        widebandCleared = true;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopBandNormalized(bandIndex, false);

        refreshBandModeButtons();
        repaint();
        return;
    }

    resetHistory();
}

void ScopView::refreshWaveform()
{
    araAnalysisColumnCount = getWaveformColumnCount();
    showingAraAnalysis = true;
    araAnalysisRevision = 0;
    displayedAraAnalysis.reset();
    clearedBands.fill(false);

    for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
        processor.setScopBandNormalized(bandIndex, false);

    if (processor.getAnalyzerPageState() == ana::AnalyzerPage::scop)
    {
        if (onAraUpdateStatus)
            onAraUpdateStatus("UPDATING 00");
        processor.requestAraAnalysis(araAnalysisColumnCount, true);
    }

    repaint();
}

void ScopView::equalizeBandHeights()
{
    bandHeightWeights.fill(1.0f);
    processor.setScopBandHeightWeights(bandHeightWeights);
    draggedBandSeparator = -1;
    refreshBandModeButtons();
    repaint();
}

void ScopView::refreshDisplaySettings()
{
    if (! showingAraAnalysis)
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
    if (draggedBandSeparator < 0)
    {
        const auto storedWeights = processor.getScopBandHeightWeights();
        const auto localTotal = std::max(0.001f, std::accumulate(
            bandHeightWeights.begin(), bandHeightWeights.end(), 0.0f));
        auto changed = false;
        for (size_t index = 0; index < bandHeightWeights.size(); ++index)
            changed = changed
                || std::abs(bandHeightWeights[index] / localTotal - storedWeights[index]) > 1.0e-4f;
        if (changed)
        {
            bandHeightWeights = storedWeights;
            resized();
        }
    }

    const auto waveformColumnCount = getWaveformColumnCount();
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    std::array<ana::ScopChannelMode, ana::dsp::LinkwitzRileyCrossover::numBands> currentModes;
    auto channelModeChanged = false;

    if (fullSourceView != processor.isScopFullSourceView())
        setFullSourceView(processor.isScopFullSourceView());

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = processor.getScopChannelMode(bandIndex);

        if (history.channelModes[bandIndex] != currentModes[bandIndex])
        {
            history.channelModes[bandIndex] = currentModes[bandIndex];
            processor.setScopBandNormalized(bandIndex, false);
            channelModeChanged = true;
        }
    }

    refreshBandModeButtons();

    if (processor.getAnalyzerPageState() != ana::AnalyzerPage::scop)
        return;

    if (! showingAraAnalysis)
    {
        araAnalysisColumnCount = waveformColumnCount;
        showingAraAnalysis = true;
        araAnalysisRevision = 0;
        displayedAraAnalysis.reset();
        clearedBands.fill(false);
        widebandCleared = false;

        for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
            processor.setScopBandNormalized(bandIndex, false);

        if (onAraUpdateStatus)
            onAraUpdateStatus("UPDATING 00");
        processor.requestAraAnalysis(araAnalysisColumnCount, true);
        repaint();
    }

    if (araAnalysisColumnCount == 0)
        araAnalysisColumnCount = waveformColumnCount;

    processor.requestAraAnalysis(araAnalysisColumnCount);

    if (const auto analysis = processor.getAraAnalysisResult())
    {
        if (! showingAraAnalysis
            || araAnalysisRevision != analysis->revision
            || channelModeChanged
            || history.bandCount != activeBandCount)
            renderAraAnalysis(analysis);
    }
    return;
}

void ScopView::paint(juce::Graphics& graphics)
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    const auto filledStyle = processor.isScopFilledStyle();
    graphics.setColour(ana::ui::opacityShade(processor.getScopOpacity()));

    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
    {
        if ((fullSourceView && widebandCleared)
            || (! fullSourceView && clearedBands[bandIndex]))
            continue;

        const auto laneBounds = getBandBounds(bandIndex, activeBandCount);

        if (laneBounds.isEmpty())
            continue;

        auto waveformBounds = laneBounds;
        const auto zoomSlidersFit = shouldShowZoomSliders(bandIndex, activeBandCount);
        if (zoomSlidersFit && processor.areScopHorizontalZoomControlsVisible())
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));
        if (zoomSlidersFit && processor.areScopVerticalZoomControlsVisible())
            waveformBounds.setRight(std::max(
                waveformBounds.getX() + 1.0f,
                static_cast<float>(getWidth() - waveformRightInset)));

        const auto rangeStart = bandRangeSliders[bandIndex]->getRangeStart();
        const auto rangeEnd = bandRangeSliders[bandIndex]->getRangeEnd();
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
            const auto* envelope = showingAraAnalysis && displayedAraAnalysis != nullptr
                ? (fullSourceView
                    ? &displayedAraAnalysis->wideband[modeIndex]
                    : &displayedAraAnalysis->bands[bandIndex][modeIndex])
                : ! showingAraAnalysis
                    ? (fullSourceView
                        ? &history.wideband[modeIndex]
                        : &history.bands[bandIndex][modeIndex])
                    : nullptr;

            if (envelope != nullptr)
                drawWaveformEnvelope(graphics, envelope->minimums, envelope->maximums, displayBounds,
                                     rangeStart, rangeEnd,
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

        const auto zoomSlidersFit = shouldShowZoomSliders(bandIndex, activeBandCount);
        const auto showHorizontalZoom = zoomSlidersFit
            && processor.areScopHorizontalZoomControlsVisible();
        const auto showVerticalZoom = zoomSlidersFit
            && processor.areScopVerticalZoomControlsVisible();

        if (showHorizontalZoom)
            waveformBounds.setBottom(std::max(waveformBounds.getY() + 1.0f,
                                              waveformBounds.getBottom()
                                                  - static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels())));

        const auto lineWidth = showVerticalZoom
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
    juce::ignoreUnused(sizeChanged);

    refreshBandModeButtons();
    repaint();
}

void ScopView::resetHistory()
{
    showingAraAnalysis = false;
    araAnalysisColumnCount = 0;
    araAnalysisRevision = 0;
    displayedAraAnalysis.reset();
    clearedBands.fill(false);
    widebandCleared = false;

    std::array<ana::ScopChannelMode, ana::dsp::LinkwitzRileyCrossover::numBands> modes {};
    for (size_t bandIndex = 0; bandIndex < modes.size(); ++bandIndex)
        modes[bandIndex] = processor.getScopChannelMode(bandIndex);

    history.reset(getWaveformColumnCount(),
                  processor.getScopTimeMilliseconds(),
                  processor.getCrossoverCount() + 1,
                  uint64_t { 0 },
                  modes);
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
        widebandCleared = showingAraAnalysis;
        processor.setScopBandNormalized(0, false);
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    history.clearBand(bandIndex);
    clearedBands[bandIndex] = showingAraAnalysis;
    processor.setScopBandNormalized(bandIndex, false);
    refreshBandModeButtons();
    repaint(bandBounds);
}

void ScopView::normalizeBandWithZoom(const size_t bandIndex)
{
    if (! showingAraAnalysis
        || displayedAraAnalysis == nullptr
        || bandIndex >= history.bandCount
        || (fullSourceView ? widebandCleared : clearedBands[bandIndex]))
        return;

    if (processor.isScopBandNormalized(bandIndex))
    {
        processor.setScopBandNormalized(bandIndex, false);
        processor.setScopVerticalZoomDecibels(bandIndex, 0.0f);
        refreshBandModeButtons();
        repaint();
        return;
    }

    auto peak = 0.0f;
    size_t validColumnCount = 0;
    const auto displayedModes = getDisplayedModes(processor.getScopChannelMode(bandIndex));

    for (size_t displayIndex = 0; displayIndex < displayedModes.count; ++displayIndex)
    {
        const auto& envelope = fullSourceView
            ? displayedAraAnalysis->wideband[displayedModes.indices[displayIndex]]
            : displayedAraAnalysis->bands[bandIndex][displayedModes.indices[displayIndex]];

        if (envelope.minimums.empty()
            || envelope.maximums.size() != envelope.minimums.size())
            continue;

        for (size_t index = 0; index < envelope.minimums.size(); ++index)
        {
            if (envelope.minimums[index] <= envelope.maximums[index])
            {
                peak = std::max(peak,
                    std::max(std::abs(envelope.minimums[index]),
                             std::abs(envelope.maximums[index])));
                ++validColumnCount;
            }
        }
    }

    if (validColumnCount == 0 || peak <= 1.0e-6f)
        return;

    const auto normalizationZoom = juce::jlimit(
        ana::scop::minimumVerticalZoomDecibels, ana::scop::maximumVerticalZoomDecibels,
        juce::Decibels::gainToDecibels(1.0f / peak));
    processor.setScopVerticalZoomDecibels(bandIndex, normalizationZoom);
    processor.setScopBandNormalized(bandIndex, true);
    refreshBandModeButtons();
    repaint();
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
    const auto zoomValueWidth = bandZoomValueWidth();
    const auto showZoomControls = processor.areScopVerticalZoomControlsVisible();
    const auto showVerticalReadout = processor.areScopVerticalReadoutsVisible();
    const auto showHorizontalZoomControls = processor.areScopHorizontalZoomControlsVisible();
    const auto showHorizontalReadouts = processor.areScopHorizontalReadoutsVisible();
    const auto showMonitorControls = processor.areScopMonitorControlsVisible();
    const auto showTools = processor.areScopToolsVisible();
    const auto normalizationAvailable = showingAraAnalysis
        && displayedAraAnalysis != nullptr;
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
        const auto normalized = processor.isScopBandNormalized(bandIndex);
        const auto laneBounds = isVisibleBand
            ? getBandBounds(bandIndex, activeBandCount).toNearestInt()
            : juce::Rectangle<int>();
        const auto showZoomSliders = isVisibleBand
            && shouldShowZoomSliders(bandIndex, activeBandCount);
        const auto showVerticalZoomSlider = showZoomSliders && showZoomControls;
        const auto controlsY = laneBounds.getY() + ana::ui::gap.pixels();
        const auto normalizeButtonWidth = bandNormalizeButtons[bandIndex]->getPreferredWidth();
        const auto preferredZoomControlsWidth = normalizeButtonWidth
            + (showVerticalReadout ? ana::ui::gap.pixels() + zoomValueWidth : 0)
            + (showVerticalZoomSlider ? ana::ui::gap.pixels() + zoomSliderWidth : 0);
        const auto leftControlsWidth = std::max(0, getWidth() - preferredZoomControlsWidth);
        auto controlX = 0;
        auto controlY = controlsY;
        const auto placeLeftControl = [&] (juce::Component& component, const int width)
        {
            if (controlX + width > leftControlsWidth)
            {
                controlX = 0;
                controlY += buttonHeight + ana::ui::gap.pixels();
            }
            component.setBounds(controlX, controlY, width, buttonHeight);
            controlX += width + ana::ui::gap.pixels();
        };

        for (size_t modeIndex = 0; modeIndex < bandModeButtons[bandIndex].size(); ++modeIndex)
        {
            auto& button = *bandModeButtons[bandIndex][modeIndex];
            button.setVisible(isVisibleBand && showMonitorControls);
            button.setToggleState(selectedMode == scopModeButtonModes[modeIndex],
                                  juce::dontSendNotification);

            if (isVisibleBand && showMonitorControls)
                placeLeftControl(button, button.getPreferredWidth());
        }

        auto& clearButton = *bandClearButtons[bandIndex];
        clearButton.setVisible(isVisibleBand && showTools);
        if (isVisibleBand && showTools)
            placeLeftControl(clearButton, clearButton.getPreferredWidth());

        auto& singleViewButton = *bandSingleViewButtons[bandIndex];
        singleViewButton.setVisible(isVisibleBand && showTools && ! fullSourceView);
        singleViewButton.setToggleState(singleViewBand == static_cast<int>(bandIndex),
                                        juce::dontSendNotification);
        if (isVisibleBand && showTools)
            placeLeftControl(singleViewButton, singleViewButton.getPreferredWidth());

        auto& zoomSlider = *bandZoomSliders[bandIndex];
        auto& zoomValueLabel = *bandZoomValueLabels[bandIndex];
        zoomSlider.setVisible(showVerticalZoomSlider);
        zoomValueLabel.setVisible(isVisibleBand && showVerticalReadout);
        zoomSlider.setValue(verticalZoomDecibels, juce::dontSendNotification);
        zoomSlider.setTooltip("ZOOM " + formatZoomValue(verticalZoomDecibels));
        zoomValueLabel.setText(formatZoomValue(verticalZoomDecibels), juce::dontSendNotification);
        auto& normalizeButton = *bandNormalizeButtons[bandIndex];
        normalizeButton.setVisible(isVisibleBand);
        normalizeButton.setEnabled(normalizationAvailable
                                   && ! (fullSourceView ? widebandCleared : clearedBands[bandIndex]));
        normalizeButton.setToggleState(normalized, juce::dontSendNotification);

        auto& rangeSlider = *bandRangeSliders[bandIndex];
        rangeSlider.setRange(
            readParameterValue(processor, PluginProcessor::scopRangeStartParameterIds[bandIndex], 0.0f),
            readParameterValue(processor, PluginProcessor::scopRangeEndParameterIds[bandIndex], 1.0f));
        rangeSlider.setVisible(showZoomSliders && showHorizontalZoomControls);

        auto& timeStartLabel = *bandTimeStartLabels[bandIndex];
        auto& timeEndLabel = *bandTimeEndLabels[bandIndex];
        const auto startSeconds = displayedAraAnalysis != nullptr
            ? displayedAraAnalysis->startTimeSeconds
                + displayedAraAnalysis->durationSeconds * rangeSlider.getRangeStart()
            : 0.0;
        const auto endSeconds = displayedAraAnalysis != nullptr
            ? displayedAraAnalysis->startTimeSeconds
                + displayedAraAnalysis->durationSeconds * rangeSlider.getRangeEnd()
            : 0.0;
        timeStartLabel.setText(formatTimeReadout(startSeconds), juce::dontSendNotification);
        timeEndLabel.setText(formatTimeReadout(endSeconds), juce::dontSendNotification);
        const auto timeReadoutWidth = bandTimeReadoutWidth();
        const auto showTimeReadouts = isVisibleBand && showHorizontalReadouts;
        timeStartLabel.setVisible(showTimeReadouts);
        timeEndLabel.setVisible(showTimeReadouts);
        if (showTimeReadouts)
        {
            placeLeftControl(timeStartLabel, timeReadoutWidth);
            placeLeftControl(timeEndLabel, timeReadoutWidth);
        }

        if (showZoomSliders && showHorizontalZoomControls)
        {
            const auto rangeWidth = showVerticalZoomSlider
                ? getWidth() - zoomSliderWidth - ana::ui::gap.pixels()
                : getWidth();
            rangeSlider.setBounds(0,
                                  laneBounds.getBottom() - ana::ui::gap.pixels() - bandRangeSliderHeight,
                                  std::max(1, rangeWidth), bandRangeSliderHeight);
        }

        if (isVisibleBand)
        {
            const auto buttonY = controlsY;
            const auto sliderX = showVerticalZoomSlider
                ? std::max(0, getWidth() - zoomSliderWidth)
                : getWidth();
            const auto zoomValueX = sliderX - (showVerticalZoomSlider ? ana::ui::gap.pixels() : 0)
                - zoomValueWidth;
            if (showVerticalReadout)
                zoomValueLabel.setBounds(zoomValueX, buttonY, zoomValueWidth, buttonHeight);
            const auto normalizeButtonX = (showVerticalReadout
                ? zoomValueX - ana::ui::gap.pixels()
                : sliderX - (showVerticalZoomSlider ? ana::ui::gap.pixels() : 0))
                - normalizeButtonWidth;
            normalizeButton.setBounds(normalizeButtonX, buttonY, normalizeButtonWidth, buttonHeight);

            if (showVerticalZoomSlider)
            {
                zoomSlider.setBounds(sliderX, buttonY, zoomSliderWidth,
                                     std::max(1, laneBounds.getBottom()
                                         - ana::ui::gap.pixels() - buttonY));
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
    return getBandBounds(bandIndex, activeBandCount).getHeight()
            >= static_cast<float>(minimumBandHeight);
}

bool ScopView::hasVisibleZoomSliders() const noexcept
{
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        if (processor.areScopVerticalZoomControlsVisible()
            && shouldShowZoomSliders(bandIndex, activeBandCount))
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
    const auto wasDragging = draggedBandSeparator >= 0;
    draggedBandSeparator = -1;
    if (wasDragging)
        processor.setScopBandHeightWeights(bandHeightWeights);
    mouseMove(event);
}


void ScopView::renderAraAnalysis(
    std::shared_ptr<const ana::ara::AnalysisResult> analysis)
{
    if (analysis == nullptr)
        return;

    const auto receivedNewRevision = araAnalysisRevision != analysis->revision;
    history.bandCount = std::min(analysis->activeBandCount,
                                processor.getCrossoverCount() + 1);
    araAnalysisRevision = analysis->revision;
    showingAraAnalysis = true;
    displayedAraAnalysis = std::move(analysis);
    clearedBands.fill(false);
    widebandCleared = false;

    if (receivedNewRevision)
    {
        refreshBandModeButtons();

        if (onAraUpdateStatus)
            onAraUpdateStatus(
                "UPDATED");
    }

    repaint();
}
