#include "View.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/CleanView.h"
#include "shell/Processor.h"
#include "shared/shell/Theme.h"
#include "shared/scop/Layout.h"
#include "shared/scop/Display.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

using ana::ui::analyzer_detail::readParameterValue;
using ana::ui::analyzer_detail::setParameterPlainValue;

using namespace ana::scop::display;

namespace
{
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

}

OfflineScopView::OfflineScopView(PluginProcessor& processorRef)
    : ana::scop::BandView(processorRef)
{
    setName("scop-graph");
    createBandControls();

    for (size_t bandIndex = 0; bandIndex < displayedChannelModes.size(); ++bandIndex)
        displayedChannelModes[bandIndex] = getDisplayedChannelMode(bandIndex);
    refreshBandModeButtons();
    startTimerHz(60);
}

void OfflineScopView::createBandExtras(const size_t bandIndex)
{
    const auto makeTimeLabel = [this]
    {
        auto label = std::make_unique<EllipsisLabel>();
        label->setFont(ana::ui::makeFont());
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, ana::ui::white);
        label->setColour(juce::Label::textWhenEditingColourId, ana::ui::white);
        label->setColour(juce::Label::backgroundColourId, ana::ui::dark);
        label->setColour(juce::Label::outlineColourId, ana::ui::light);
        label->setBorderSize(juce::BorderSize<int>(1));
        label->setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
        label->setEditable(true, false, false);
        addAndMakeVisible(*label);
        return label;
    };
    bandTimeStartLabels[bandIndex] = makeTimeLabel();
    bandTimeEndLabels[bandIndex] = makeTimeLabel();

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

    const auto editTime = [this, bandIndex] (const bool start)
    {
        auto& label = *(start ? bandTimeStartLabels[bandIndex] : bandTimeEndLabels[bandIndex]);
        const auto text = label.getText().trim();
        if (displayedOfflineAnalysis != nullptr
            && displayedOfflineAnalysis->durationSeconds > 0.0
            && text.isNotEmpty() && text.containsOnly("0123456789.:")
            && text.containsAnyOf("0123456789"))
        {
            const auto fields = juce::StringArray::fromTokens(text, ":", "");
            if (fields.size() == 1 || fields.size() == 2)
            {
                const auto seconds = fields.size() == 2
                    ? fields[0].getDoubleValue() * 60.0 + fields[1].getDoubleValue()
                    : fields[0].getDoubleValue();
                auto& slider = *bandRangeSliders[bandIndex];
                const auto position = static_cast<float>((seconds
                    - displayedOfflineAnalysis->startTimeSeconds)
                    / displayedOfflineAnalysis->durationSeconds);
                slider.setRange(start ? juce::jlimit(0.0f, std::max(0.0f, slider.getRangeEnd() - 0.01f), position)
                                      : slider.getRangeStart(),
                                start ? slider.getRangeEnd()
                                      : juce::jlimit(std::min(1.0f, slider.getRangeStart() + 0.01f), 1.0f, position));
            }
        }
        refreshBandModeButtons();
    };
    bandTimeStartLabels[bandIndex]->onTextChange = [editTime] { editTime(true); };
    bandTimeEndLabels[bandIndex]->onTextChange = [editTime] { editTime(false); };
}

void OfflineScopView::refreshBandExtras(
    const size_t bandIndex, const juce::Rectangle<int> laneBounds, const bool isVisibleBand,
    const bool zoomAvailable, const bool showVerticalZoomSlider,
    const int topControlsBottom)
{
    auto& rangeSlider = *bandRangeSliders[bandIndex];
    rangeSlider.setRange(
        readParameterValue(processor, PluginProcessor::scopRangeStartParameterIds[bandIndex], 0.0f),
        readParameterValue(processor, PluginProcessor::scopRangeEndParameterIds[bandIndex], 1.0f));
    const auto showRangeSlider = zoomAvailable && processor.areScopHorizontalZoomControlsVisible();
    rangeSlider.setVisible(showRangeSlider);

    auto& timeStartLabel = *bandTimeStartLabels[bandIndex];
    auto& timeEndLabel = *bandTimeEndLabels[bandIndex];
    const auto startSeconds = displayedOfflineAnalysis != nullptr
        ? displayedOfflineAnalysis->startTimeSeconds
            + displayedOfflineAnalysis->durationSeconds * rangeSlider.getRangeStart()
        : 0.0;
    const auto endSeconds = displayedOfflineAnalysis != nullptr
        ? displayedOfflineAnalysis->startTimeSeconds
            + displayedOfflineAnalysis->durationSeconds * rangeSlider.getRangeEnd()
        : 0.0;
    if (! timeStartLabel.isBeingEdited())
        timeStartLabel.setText(formatTimeReadout(startSeconds), juce::dontSendNotification);
    if (! timeEndLabel.isBeingEdited())
        timeEndLabel.setText(formatTimeReadout(endSeconds), juce::dontSendNotification);
    // RO-HORIZ belongs at the two lower corners of the waveform, immediately
    // above the horizontal zoom slider. It must not join the upper tools row.
    const auto gap = ana::ui::gap.pixels();
    const auto rangeWidth = showVerticalZoomSlider
        ? getWidth() - ana::scop::bandZoomSliderWidth - gap
        : getWidth();
    const auto rangeY = laneBounds.getBottom() - gap - ana::scop::bandRangeSliderHeight;
    const auto readoutY = (showRangeSlider ? rangeY - gap : laneBounds.getBottom() - gap)
        - ana::ui::controlHeight;
    const auto timeReadoutWidth = bandTimeReadoutWidth();
    const auto showTimeReadouts = isVisibleBand
        && ! processor.isCleanView()
        && processor.areScopHorizontalReadoutsVisible();
    juce::ignoreUnused(topControlsBottom);

    timeStartLabel.setVisible(showTimeReadouts);
    timeEndLabel.setVisible(showTimeReadouts);
    if (showTimeReadouts)
    {
        timeStartLabel.setBounds(0, readoutY, timeReadoutWidth, ana::ui::controlHeight);
        timeEndLabel.setBounds(rangeWidth - timeReadoutWidth, readoutY,
                               timeReadoutWidth, ana::ui::controlHeight);
    }

    if (showRangeSlider)
        rangeSlider.setBounds(0, rangeY, std::max(1, rangeWidth),
                              ana::scop::bandRangeSliderHeight);
}

bool OfflineScopView::isBandNormalizationEnabled(const size_t bandIndex) const
{
    return displayedOfflineAnalysis != nullptr
        && bandIndex < displayedBandCount
        && ! (fullSourceView ? widebandCleared : clearedBands[bandIndex]);
}

int OfflineScopView::minimumBandHeight() const noexcept
{
    return ana::scop::minimumBandHeightForControls(true,true);
}

const ana::scop::Envelope* OfflineScopView::getBandEnvelope(
    const size_t bandIndex, const size_t channelIndex) const noexcept
{
    if (displayedOfflineAnalysis == nullptr || bandIndex >= displayedBandCount)
        return nullptr;
    const auto& envelopes = fullSourceView
        ? displayedOfflineAnalysis->wideband
        : displayedOfflineAnalysis->bands[bandIndex];
    return channelIndex < envelopes.size() ? &envelopes[channelIndex] : nullptr;
}

void OfflineScopView::refreshWaveform()
{
    offlineAnalysisColumnCount = getWaveformColumnCount();
    offlineAnalysisRevision = 0;
    displayedOfflineAnalysis.reset();
    clearedBands.fill(false);

    // Keep N enabled across refreshes; apply the new peak when the analysis arrives.
    if (processor.getAnalyzerPageState() == ana::AnalyzerPage::scop)
    {
        processor.requestOfflineAnalysis(offlineAnalysisColumnCount, true);
    }

    repaint();
}

void OfflineScopView::refreshDisplaySettings()
{
    refreshBandModeButtons();
    repaint();
}

void OfflineScopView::timerCallback()
{
    if (!isShowing()) return;
    if (!processor.isOfflineMode())
        return;
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
        currentModes[bandIndex] = getDisplayedChannelMode(bandIndex);

        if (displayedChannelModes[bandIndex] != currentModes[bandIndex])
        {
            displayedChannelModes[bandIndex] = currentModes[bandIndex];
            channelModeChanged = true;
        }
    }

    refreshBandModeButtons();

    if (processor.getAnalyzerPageState() != ana::AnalyzerPage::scop)
        return;

    if (offlineAnalysisColumnCount == 0)
        offlineAnalysisColumnCount = waveformColumnCount;

    processor.requestOfflineAnalysis(offlineAnalysisColumnCount);

    if (const auto analysis = processor.getAnalysisResult())
    {
        if (displayedOfflineAnalysis == nullptr
            || offlineAnalysisRevision != analysis->revision
            || channelModeChanged
            || displayedBandCount != activeBandCount)
            renderOfflineAnalysis(analysis);
    }
    return;
}

void OfflineScopView::paint(juce::Graphics& graphics)
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

        // Zoom controls overlay the lane, as in realtime. Reserving space
        // only below the waveform shifts its zero line above the lane centre.
        auto waveformBounds = laneBounds;
        waveformBounds.setRight(std::max(waveformBounds.getX()+1.0f,static_cast<float>(getWidth()-ana::scop::waveformRightInset())));

        const auto rangeStart = bandRangeSliders[bandIndex]->getRangeStart();
        const auto rangeEnd = bandRangeSliders[bandIndex]->getRangeEnd();
        const auto mode = getDisplayedChannelMode(bandIndex);
        const auto displayedModes = getDisplayedModes(mode);

        if (mode == ana::ScopChannelMode::st)
        {
            if (displayedOfflineAnalysis != nullptr)
            {
                const auto& stereo = fullSourceView
                    ? displayedOfflineAnalysis->wideband
                    : displayedOfflineAnalysis->bands[bandIndex];
                drawStereoWaveformEnvelope(graphics, stereo[0], stereo[1], waveformBounds,
                                           rangeStart, rangeEnd,
                                           processor.getScopVerticalZoomDecibels(bandIndex),
                                           filledStyle);
            }
            continue;
        }

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
            const auto* envelope = displayedOfflineAnalysis != nullptr
                ? (fullSourceView
                    ? &displayedOfflineAnalysis->wideband[modeIndex]
                    : &displayedOfflineAnalysis->bands[bandIndex][modeIndex])
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

        const auto lineWidth=std::max(0,getWidth()-ana::scop::waveformRightInset());
        const auto mode = getDisplayedChannelMode(bandIndex);
        const auto displayedModes = getDisplayedModes(mode);
        const auto laneCount = mode == ana::ScopChannelMode::st ? size_t { 1 } : displayedModes.count;

        for (size_t displayIndex = 0; displayIndex < laneCount; ++displayIndex)
        {
            const auto centre = laneCount == 1
                ? waveformBounds.getCentreY()
                : waveformBounds.getY()
                    + waveformBounds.getHeight() * (static_cast<float>(displayIndex) + 0.5f) * 0.5f;
            graphics.fillRect(0, juce::roundToInt(centre), lineWidth, 1);
        }

        if (laneCount == 2)
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

void OfflineScopView::resized()
{
    refreshBandModeButtons();
    repaint();
}

void OfflineScopView::clearBandHistory(const size_t bandIndex)
{
    if (displayedOfflineAnalysis == nullptr || bandIndex >= displayedBandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, displayedBandCount).toNearestInt();
    if (fullSourceView)
    {
        widebandCleared = true;
        processor.setScopBandNormalized(0, false);
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    clearedBands[bandIndex] = true;
    processor.setScopBandNormalized(bandIndex, false);
    refreshBandModeButtons();
    repaint(bandBounds);
}

void OfflineScopView::renderOfflineAnalysis(
    std::shared_ptr<const ana::offline::AnalysisResult> analysis)
{
    if (analysis == nullptr)
        return;

    const auto receivedNewRevision = offlineAnalysisRevision != analysis->revision;
    displayedBandCount = std::min(analysis->activeBandCount,
                                processor.getCrossoverCount() + 1);
    offlineAnalysisRevision = analysis->revision;
    displayedOfflineAnalysis = std::move(analysis);
    clearedBands.fill(false);
    widebandCleared = false;

    // A new offline revision may have a different peak: update every enabled N.
    refreshActiveBandNormalizations();
    if (receivedNewRevision)
        refreshBandModeButtons();

    repaint();
}
