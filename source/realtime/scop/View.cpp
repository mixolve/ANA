#include "View.h"
#include "shared/shell/PlotVisibility.h"
#include "Processor.h"
#include "shell/Processor.h"
#include "shared/shell/Theme.h"
#include "shared/scop/Layout.h"
#include "shared/scop/Display.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

using namespace ana::scop::display;


RealtimeScopView::RealtimeScopView(PluginProcessor& processorRef)
    : ana::scop::BandView(processorRef)
{
    setName("scop-graph");
    clearRevision=processor.getScopClearRevision();
    createBandControls();
    resetHistory();
    refreshBandModeButtons();
    startTimerHz(60);
}

bool RealtimeScopView::isBandNormalizationEnabled(const size_t bandIndex) const
{
    return bandIndex < history.bandCount;
}

int RealtimeScopView::minimumBandHeight() const noexcept
{
    return ana::scop::minimumRealtimeBandHeight;
}

const ana::scop::Envelope* RealtimeScopView::getBandEnvelope(
    const size_t bandIndex, const size_t channelIndex) const noexcept
{
    if (bandIndex >= history.bandCount)
        return nullptr;
    const auto& envelopes = fullSourceView ? history.wideband : history.bands[bandIndex];
    return channelIndex < envelopes.size() ? &envelopes[channelIndex] : nullptr;
}

void RealtimeScopView::setFullSourceView(const bool shouldShowFullSource)
{
    if (fullSourceView == shouldShowFullSource)
        return;
    ana::scop::BandView::setFullSourceView(shouldShowFullSource);
    // The long-window overview and the multiband view have different
    // accumulation granularities. Do not splice the two histories together.
    resetHistory();
}

void RealtimeScopView::setFrozen(const bool shouldFreeze)
{
    if (frozen == shouldFreeze)
        return;

    frozen = shouldFreeze;

    if (! frozen)
    {
        history.readCursor = processor.getMultibandScop().getWriteCursor();
        history.readPeakCursor = processor.getMultibandScop().getPeakWriteCursor();
        history.columnSampleProgress = 0.0;
        history.resetColumnAccumulator();
    }
}

void RealtimeScopView::clearHistory()
{
    resetHistory();
    for (size_t bandIndex = 0; bandIndex < bandNormalizeButtons.size(); ++bandIndex)
        processor.setScopBandNormalized(bandIndex, false);
    refreshBandModeButtons();
}

void RealtimeScopView::refreshDisplaySettings()
{
    resizeHistory(getWaveformColumnCount());

    refreshBandModeButtons();
    repaint();
}

void RealtimeScopView::timerCallback()
{
    if (processor.isOfflineMode())
        return;
    if (!isShowing())
    {
        // This timer otherwise drains and rebins the whole SCOP audio ring at
        // 60 Hz even when FREQ, MAP, LVLS or CORR is on screen.
        suspendedWhileHidden = true;
        return;
    }
    if (suspendedWhileHidden)
    {
        suspendedWhileHidden = false;
        // Do not replay an entire hidden interval on the message thread.
        // Start the visible live trace at the current audio write cursor.
        resetHistory();
    }
    const auto revision=processor.getScopClearRevision();
    if(revision!=clearRevision) { clearRevision=revision; clearHistory(); }
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

    const auto timeMilliseconds = processor.getScopTimeMilliseconds();
    const auto activeBandCount = processor.getCrossoverCount() + 1;
    std::array<ana::ScopChannelMode, ana::MultibandScop::numBands> currentModes;

    if (fullSourceView != processor.isScopFullSourceView())
        setFullSourceView(processor.isScopFullSourceView());

    for (size_t bandIndex = 0; bandIndex < currentModes.size(); ++bandIndex)
    {
        currentModes[bandIndex] = getDisplayedChannelMode(bandIndex);

        if (history.channelModes[bandIndex] != currentModes[bandIndex])
        {
            history.channelModes[bandIndex] = currentModes[bandIndex];
        }
    }

    refreshBandModeButtons();

    const auto* leftToRightParameter = processor.getRawParameterValue(
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

    // A stream reset or a newly installed SLOPE kernel changes which DSP
    // produces the samples.  Keeping the preceding envelope would splice
    // together differently filtered waveforms in one realtime trace.
    const auto& scopState = processor.getMultibandScop();
    if (observedResetRevision != scopState.getResetRevision()
        || observedSlopeRevision != scopState.getSlopeRevision())
    {
        resetHistory();
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
    const auto visibleHistoryColumns = std::max<size_t>(
        1, history.bands.front().front().minimums.size());
    const auto samplesPerColumn = std::max(
        0.001,
        scop.getSampleRate() * timeMilliseconds * 0.001
            / static_cast<double>(visibleHistoryColumns));

    // For zoomed-out FULL displays preserve audio-thread extrema over long
    // windows rather than transferring millions of individual samples through
    // the GUI timer. At short timebases use the native ring for sample detail.
    const auto aggregatePeaks = fullSourceView
        && samplesPerColumn >= static_cast<double>(ana::MultibandScop::peakBlockSize * 8);
    if (usingPeakAggregation != aggregatePeaks)
    {
        resetHistory();
        return;
    }
    if (aggregatePeaks)
        scop.copyWidebandPeaksSince(incomingSamples, history.readPeakCursor);
    else if (fullSourceView)
        scop.copyWidebandSince(incomingSamples, history.readCursor);
    else
        scop.copySince(incomingSamples, history.readCursor);

    const auto sampleCount = incomingSamples.widebandMinimums.front().size();
    if (sampleCount == 0)
        return;

    const auto sampleStride = aggregatePeaks
        ? static_cast<double>(ana::MultibandScop::peakBlockSize) : 1.0;
    const auto renderedBandCount = fullSourceView ? size_t { 0 } : activeBandCount;
    auto appendedColumn = false;

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        ana::scop::AnalysisChannelSamples widebandMinimums {};
        ana::scop::AnalysisChannelSamples widebandMaximums {};
        for (size_t modeIndex = 0; modeIndex < widebandMinimums.size(); ++modeIndex)
        {
            widebandMinimums[modeIndex] = incomingSamples.widebandMinimums[modeIndex][sampleIndex];
            widebandMaximums[modeIndex] = incomingSamples.widebandMaximums[modeIndex][sampleIndex];
        }
        history.accumulateWidebandExtrema(widebandMinimums, widebandMaximums);

        for (size_t bandIndex = 0; bandIndex < renderedBandCount; ++bandIndex)
            history.accumulateBand(bandIndex, getRealtimeAnalysisChannelSamples(bandIndex, sampleIndex));

        history.columnSampleProgress += sampleStride;
        const auto columnsToAppend = static_cast<size_t>(history.columnSampleProgress / samplesPerColumn);
        if (columnsToAppend == 0)
            continue;

        for (size_t columnIndex = 0; columnIndex < columnsToAppend; ++columnIndex)
            appendHistoryColumn(renderedBandCount);

        history.columnSampleProgress -= static_cast<double>(columnsToAppend) * samplesPerColumn;
        history.resetColumnAccumulator();
        // A 64-sample group can straddle a pixel boundary. Both adjacent
        // pixels must retain its extrema, otherwise a peak at that boundary
        // may be dropped. This affects only the long-timebase overview.
        if (aggregatePeaks && history.columnSampleProgress > 0.0)
            history.accumulateWidebandExtrema(widebandMinimums, widebandMaximums);
        appendedColumn = true;
    }

    if (appendedColumn)
    {
        refreshActiveBandNormalizations();
        repaint();
    }
}

void RealtimeScopView::paint(juce::Graphics& graphics)
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
        waveformBounds.setRight(std::max(waveformBounds.getX()+1.0f,
            static_cast<float>(getWidth()-ana::scop::waveformRightInset())));

        const auto mode = getDisplayedChannelMode(bandIndex);
        const auto displayedModes = getDisplayedModes(mode);

        if (mode == ana::ScopChannelMode::st)
        {
            const auto& stereo = fullSourceView ? history.wideband : history.bands[bandIndex];
            drawStereoWaveformEnvelope(graphics, stereo[0], stereo[1], waveformBounds,
                                       0.0f, 1.0f,
                                       processor.getScopVerticalZoomDecibels(bandIndex),
                                       filledStyle);
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

void RealtimeScopView::resized()
{
    const auto sizeChanged = lastComponentWidth != getWidth()
        || lastComponentHeight != getHeight();
    lastComponentWidth = getWidth();
    lastComponentHeight = getHeight();

    // A window resize must not drop live samples.  Rebin the existing
    // envelope to the new pixel count while processing continues normally.
    const auto columnCount = getWaveformColumnCount();
    if (sizeChanged || history.bands.front().front().minimums.size() != columnCount)
        resizeHistory(columnCount);

    refreshBandModeButtons();
    repaint();
}

void RealtimeScopView::resetHistory()
{
    const auto& scopState = processor.getMultibandScop();
    observedResetRevision = scopState.getResetRevision();
    observedSlopeRevision = scopState.getSlopeRevision();

    std::array<ana::ScopChannelMode, ana::MultibandScop::numBands> modes {};
    for (size_t bandIndex = 0; bandIndex < modes.size(); ++bandIndex)
        modes[bandIndex] = getDisplayedChannelMode(bandIndex);

    history.reset(getWaveformColumnCount(),
                  processor.getScopTimeMilliseconds(),
                  processor.getCrossoverCount() + 1,
                  scopState.getWriteCursor(),
                  modes);
    history.readPeakCursor = scopState.getPeakWriteCursor();
    const auto pixelCount = std::max<size_t>(1, getWaveformColumnCount());
    const auto samplesPerColumn = scopState.getSampleRate()
        * processor.getScopTimeMilliseconds() * 0.001 / static_cast<double>(pixelCount);
    usingPeakAggregation = fullSourceView
        && samplesPerColumn >= static_cast<double>(ana::MultibandScop::peakBlockSize * 8);
    leftToRightColumn = 0;
    repaint();
}

void RealtimeScopView::resizeHistory(const size_t newColumnCount)
{
    history.resize(newColumnCount);
    repaint();
}

void RealtimeScopView::clearBandHistory(const size_t bandIndex)
{
    if (bandIndex >= history.bandCount)
        return;

    const auto bandBounds = getBandBounds(bandIndex, history.bandCount).toNearestInt();
    if (fullSourceView)
    {
        history.clearWideband();
        processor.setScopBandNormalized(bandIndex, false);
        refreshBandModeButtons();
        repaint(bandBounds);
        return;
    }

    history.clearBand(bandIndex);
    processor.setScopBandNormalized(bandIndex, false);
    refreshBandModeButtons();
    repaint(bandBounds);
}

ana::scop::AnalysisChannelSamples
RealtimeScopView::getRealtimeAnalysisChannelSamples(const size_t bandIndex,
                                                   const size_t sampleIndex) const noexcept
{
    const auto left = incomingSamples[bandIndex][0][sampleIndex];
    const auto right = incomingSamples[bandIndex][1][sampleIndex];
    return ana::scop::makeAnalysisChannelSamples(left, right);
}

void RealtimeScopView::appendHistoryColumn(const size_t activeBandCount)
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
