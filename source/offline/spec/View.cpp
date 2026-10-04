#include "View.h"
#include "shared/spec/Display.h"
#include "shared/spec/CursorNote.h"
#include "shared/shell/CleanView.h"
#include "shared/spec/Settings.h"
#include "shared/spec/SpectrumProcessing.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/spec/SpectrogramFrequencyScale.h"
#include "shared/shell/GraphColours.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace ana::ui::analyzer_detail;
using namespace ana::spec::display;

namespace
{
constexpr int cursorNotePadding = 4;

}

OfflineSpecView::OfflineSpecView(PluginProcessor& processorRef,
                                     std::vector<ana::spec::SpectrumSnapshot>& snapshotData)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), PluginProcessor::specLowParameterId,
                          "LOW", [] (const double value) { return formatReadoutFrequency(value); }),
      frequencyHighControl(processorRef.getParameters(), PluginProcessor::specHighParameterId,
                           "HIGH", [] (const double value) { return formatReadoutFrequency(value); }),
      rangeLowControl(processorRef.getParameters(), PluginProcessor::specRangeLowParameterId,
                      "RANGE-LOW", [] (const double value) { return formatReadoutLevel(value); }),
      rangeHighControl(processorRef.getParameters(), PluginProcessor::specRangeHighParameterId,
                       "RANGE-HIGH", [] (const double value) { return formatReadoutLevel(value); }),
      snapshots(snapshotData)
{
    for (auto* control : std::array<ParameterControl*, 2> {
             &frequencyLowControl, &frequencyHighControl })
        control->setWheelSpeedMultiplier(1600.0f);
    for (auto* control : std::array<ParameterControl*, 2> {
             &rangeLowControl, &rangeHighControl })
        control->setWheelSpeedMultiplier(40.0f);

    for (auto* component : std::array<juce::Component*, 6> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &frequencyRangeSlider, &magnitudeRangeSlider })
        addAndMakeVisible(*component);
    addAndMakeVisible(mapTimeRangeSlider);

    configureCursorReadoutLabel(cursorReadoutLabel);
    addAndMakeVisible(cursorReadoutLabel);

    configureCursorReadoutLabel(cursorNoteReadoutLabel);
    cursorNoteReadoutLabel.setBorderSize(
        juce::BorderSize<int>(1, cursorNotePadding, 1, cursorNotePadding));
    addAndMakeVisible(cursorNoteReadoutLabel);

    configureCursorReadoutLabel(cursorVerticalReadoutLabel);
    addAndMakeVisible(cursorVerticalReadoutLabel);

    configureCursorReadoutLabel(mapTimeStartReadoutLabel);
    configureCursorReadoutLabel(mapTimeEndReadoutLabel);
    mapTimeStartReadoutLabel.setText("00:00.000", juce::dontSendNotification);
    mapTimeEndReadoutLabel.setText("00:00.000", juce::dontSendNotification);
    addAndMakeVisible(mapTimeStartReadoutLabel);
    addAndMakeVisible(mapTimeEndReadoutLabel);

    viewMode = processor.isSpecMapView() ? ViewMode::map : ViewMode::frequency;
    freqButton.setToggleState(viewMode == ViewMode::frequency, juce::dontSendNotification);
    mapButton.setToggleState(viewMode == ViewMode::map, juce::dontSendNotification);
    freqButton.onClick = [this] { setViewMode(ViewMode::frequency); };
    mapButton.onClick = [this] { setViewMode(ViewMode::map); };
    addAndMakeVisible(freqButton);
    addAndMakeVisible(mapButton);

    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        auto button = std::make_unique<ControlButton>(
            juce::String::fromUTF8(ana::spec::monitorModeLabels[index]));
        button->onClick = [this, index]
        {
            processor.setSpecMonitorMode(static_cast<int>(index));
        };
        addAndMakeVisible(*button);
        monitorButtons[index] = std::move(button);
    }
    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }

    frequencyLowControl.onValueChanged = [this]
    {
        if (viewMode == ViewMode::map)
        {
            scheduleMapImageRebuild();
        }
        repaint();
    };
    frequencyHighControl.onValueChanged = [this]
    {
        if (viewMode == ViewMode::map)
        {
            scheduleMapImageRebuild();
        }
        repaint();
    };
    rangeLowControl.onValueChanged = [this] { repaint(); };
    rangeHighControl.onValueChanged = [this] { repaint(); };

    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    mapTimeRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateMapTimeRangeFromSlider();
    };
    mapTimeRangeSlider.onDragEnded = [this]
    {
        // Zoom is render-only; ending a drag must not schedule audio analysis.
        if (viewMode == ViewMode::map)
            scheduleMapImageRebuild();
    };
    magnitudeRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateVerticalRangeFromSlider();
    };

    // A recreated REAPER peer seeds from the published revision/raster instead of reporting a false UPDATE.
    if (const auto analysisResult = processor.getAnalysisResult())
        displayedOfflineRevision = analysisResult->revision;
    offlineSpectrogramImage = processor.getCachedOfflineSpectrogramImage();

    startTimerHz(60);
}

bool OfflineSpecView::captureSnapshot(const size_t snapshotIndex)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    ana::spec::SpectrumSnapshot captured;
    captured.colour = snapshots[snapshotIndex].colour;
    captured.gainDb = snapshots[snapshotIndex].gainDb;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);

    const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
    captured.drawSecondGraph = useSplitView;

    const auto analysisResult = processor.getAnalysisResult();
    if (analysisResult == nullptr || analysisResult->spec == nullptr)
        return false;
    const auto* displayedSpec = analysisResult->spec.get();
    captured.sampleRate = analysisResult->specSampleRate;

    const auto firstType = readSpecDisplayType(processor, PluginProcessor::specFirstGraphTypeParameterId);
    displayedSpec->copySpectrum(firstChannel, firstType, captured.primary, captured.fftSize);

    if (captured.drawSecondGraph)
    {
        auto secondaryFftSize = 0;
        const auto secondType = readSpecDisplayType(processor, PluginProcessor::specSecondGraphTypeParameterId);
        displayedSpec->copySpectrum(secondChannel, secondType, captured.secondary, secondaryFftSize);
        if (secondaryFftSize != captured.fftSize)
        {
            captured.secondary.clear();
            captured.drawSecondGraph = false;
        }
    }

    captured.hasData = captured.fftSize > 0 && ! captured.primary.empty();
    if (! captured.hasData)
        return false;

    captured.visible = true;
    snapshots[snapshotIndex] = std::move(captured);
    repaint();
    return true;
}

void OfflineSpecView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::background);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    if (viewMode == ViewMode::map)
    {
        const auto& image = offlineSpectrogramImage;
        if (image.isValid())
            graphics.drawImage(image, plotBounds);

        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
        if (useSplitView)
        {
            graphics.setColour(ana::ui::white);
            graphics.fillRect(plotBounds.getX(), plotBounds.getCentreY(),
                              plotBounds.getWidth(), 1.0f);
        }

        const auto* cursorReadout = processor.getRawParameterValue(
            PluginProcessor::specCursorReadoutParameterId);
        const auto* cursorVertical = processor.getRawParameterValue(
            PluginProcessor::specCursorVerticalReadoutParameterId);
        const auto showHorizontalCursor = cursorReadout == nullptr
            || cursorReadout->load(std::memory_order_relaxed) >= 0.5f;
        const auto showVerticalCursor = cursorVertical == nullptr
            || cursorVertical->load(std::memory_order_relaxed) >= 0.5f;
        const auto* cursorNotes = processor.getRawParameterValue(PluginProcessor::specCursorNotesParameterId);
        const auto showNotes = cursorNotes == nullptr || cursorNotes->load(std::memory_order_relaxed) >= 0.5f;
        if (! processor.isCleanView() && cursorInside && (showHorizontalCursor || showVerticalCursor || showNotes))
        {
            const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);
            graphics.setColour(ana::ui::white.withAlpha(0.65f));
            if (showVerticalCursor || showNotes)
                graphics.drawLine(cursorPane.getX(), cursorPosition.y,
                                  cursorPane.getRight(), cursorPosition.y, 0.5f);
            if (showHorizontalCursor)
                graphics.drawLine(cursorPosition.x, plotBounds.getY(),
                                  cursorPosition.x, plotBounds.getBottom(), 0.5f);
        }
        return;
    }

    int fftSize = 0;
    auto sampleRate = processor.getSpecProcessor().getSampleRate();

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
    const auto drawSecondGraph = useSplitView;
    const auto firstType = readSpecDisplayType(processor, PluginProcessor::specFirstGraphTypeParameterId);
    const auto secondType = readSpecDisplayType(processor, PluginProcessor::specSecondGraphTypeParameterId);
    const auto firstColour = readGraphColour(processor,
        PluginProcessor::specFirstGraphColourParameterId, ana::ui::defaultFirstGraphColourIndex);
    const auto secondColour = readGraphColour(processor,
        PluginProcessor::specSecondGraphColourParameterId, ana::ui::defaultSecondGraphColourIndex);
    const auto graphOpacity = readGraphOpacity(processor);
    const auto copySpectra = [&] (const ana::spec::SpecProcessor& spec)
    {
        spec.copySpectrum(firstChannel, firstType, primarySpec, fftSize);
        if (drawSecondGraph)
            spec.copySpectrum(secondChannel, secondType, secondarySpec, fftSize);
    };
    if (const auto analysisResult = processor.getAnalysisResult();
        analysisResult != nullptr && analysisResult->spec != nullptr)
    {
        sampleRate = analysisResult->specSampleRate;
        copySpectra(*analysisResult->spec);
    }

    if (useSplitView)
    {
        auto upperBounds = plotBounds;
        constexpr float dividerHeight = 1.0f;
        upperBounds.setHeight((plotBounds.getHeight() - dividerHeight) * 0.5f);
        auto lowerBounds = upperBounds.withY(upperBounds.getBottom() + dividerHeight);

        for (const auto& snapshot : snapshots)
        {
            if (! snapshot.visible || ! snapshot.hasData)
                continue;

            if (snapshot.drawSecondGraph)
                drawSpec(processor, graphics, snapshot.secondary, snapshot.fftSize, snapshot.sampleRate,
                         lowerBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
            drawSpec(processor, graphics, snapshot.primary, snapshot.fftSize, snapshot.sampleRate,
                     upperBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
        }

        graphics.setColour(ana::ui::white);
        graphics.fillRect(plotBounds.getX(), upperBounds.getBottom(), plotBounds.getWidth(), dividerHeight);
        drawSpec(processor, graphics, secondarySpec, fftSize, sampleRate, lowerBounds,
                     secondColour, secondColour.withAlpha(graphOpacity));
        drawSpec(processor, graphics, primarySpec, fftSize, sampleRate, upperBounds,
                     firstColour, firstColour.withAlpha(graphOpacity));
    }
    else
    {
        for (const auto& snapshot : snapshots)
        {
            if (! snapshot.visible || ! snapshot.hasData)
                continue;

            if (snapshot.drawSecondGraph)
                drawSpec(processor, graphics, snapshot.secondary, snapshot.fftSize, snapshot.sampleRate,
                         plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
            drawSpec(processor, graphics, snapshot.primary, snapshot.fftSize, snapshot.sampleRate,
                     plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
        }

        if (drawSecondGraph)
            drawSpec(processor, graphics, secondarySpec, fftSize, sampleRate, plotBounds,
                         secondColour, secondColour.withAlpha(graphOpacity));
        drawSpec(processor, graphics, primarySpec, fftSize, sampleRate, plotBounds,
                     firstColour, firstColour.withAlpha(graphOpacity));
    }

    const auto* cursorReadout = processor.getRawParameterValue(
        PluginProcessor::specCursorReadoutParameterId);
    const auto* cursorVertical = processor.getRawParameterValue(
        PluginProcessor::specCursorVerticalReadoutParameterId);
    const auto* cursorNotes = processor.getRawParameterValue(
        PluginProcessor::specCursorNotesParameterId);
    const auto showHorizontalCursor = cursorReadout == nullptr
        || cursorReadout->load(std::memory_order_relaxed) >= 0.5f;
    const auto showVerticalCursor = cursorVertical == nullptr
        || cursorVertical->load(std::memory_order_relaxed) >= 0.5f;
    const auto showCursorNotes = cursorNotes == nullptr
        || cursorNotes->load(std::memory_order_relaxed) >= 0.5f;
    if (! processor.isCleanView() && cursorInside && (showHorizontalCursor || showVerticalCursor || showCursorNotes))
    {
        const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);
        graphics.setColour(ana::ui::white);
        if (showHorizontalCursor || showCursorNotes)
            graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        if (showVerticalCursor)
            graphics.drawLine(cursorPane.getX(), cursorPosition.y, cursorPane.getRight(), cursorPosition.y, 0.5f);
    }
}

void OfflineSpecView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto readVisibility = [this] (const char* parameterId)
    {
        const auto* value = processor.getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto offlineMap = mapMode;
    const auto showCursorHorizontal = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorReadoutParameterId);
    const auto showCursorNotes = readVisibility(PluginProcessor::specCursorNotesParameterId);
    const auto showCursorVertical = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorVerticalReadoutParameterId);
    const auto showMonitor = readVisibility(PluginProcessor::specMonitorControlsParameterId);
    const auto showHorizontalZoom = (freqMode || offlineMap)
        && readVisibility(PluginProcessor::specHorizontalZoomParameterId);
    const auto showVerticalZoom = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specVerticalZoomParameterId);
    const auto showVerticalReadouts = readVisibility(PluginProcessor::specVerticalReadoutsParameterId);
    const auto showHorizontalReadouts = readVisibility(PluginProcessor::specHorizontalReadoutsParameterId);
    const auto frequencyReadoutWidth = ana::ui::textControlWidth(8);
    const auto levelReadoutWidth = ana::ui::textControlWidth(7);
    const auto timeReadoutWidth = ana::ui::textControlWidth(10);
    const auto mapCursorTimeWidth = timeReadoutWidth;
    const auto mapCursorFrequencyWidth = frequencyReadoutWidth;
    const auto readoutY = plotBounds.getBottom() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    const auto rangeReadoutWidth = mapMode ? frequencyReadoutWidth
        : (freqMode ? levelReadoutWidth : 0);
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(rangeReadoutWidth);
    if (rangeReadoutWidth > 0)
        ana::ui::gap.removeFromRight(topArea);

    ana::ui::FixedGapRow topControls(topArea);
    freqButton.setBounds(topControls.takeLeft(freqButton.getPreferredWidth()));
    mapButton.setBounds(topControls.takeLeft(mapButton.getPreferredWidth()));
    int monitorWidth = 0;
    for (const auto& button : monitorButtons)
        monitorWidth += button->getPreferredWidth() + (monitorWidth > 0 ? ana::ui::gap.pixels() : 0);
    const auto monitorBounds = monitorWidth <= topControls.remaining().getWidth()
        ? topControls.remaining()
        : juce::Rectangle<int>(plotBounds.getX(),
                               plotBounds.getY() + ana::ui::controlHeight + ana::ui::gap.pixels(),
                               plotBounds.getWidth(), ana::ui::controlHeight);
    auto monitorX = monitorBounds.getX();
    auto monitorY = monitorBounds.getY();
    const auto placeMonitorControl = [&] (juce::Component& component, const int width)
    {
        if (monitorX + width > monitorBounds.getRight())
        {
            monitorX = plotBounds.getX();
            monitorY += ana::ui::controlHeight + ana::ui::gap.pixels();
        }
        component.setBounds(monitorX, monitorY, width, ana::ui::controlHeight);
        monitorX += width + ana::ui::gap.pixels();
    };
    for (auto& button : monitorButtons)
    {
        button->setBounds({});
        if (showMonitor)
            placeMonitorControl(*button, button->getPreferredWidth());
    }

    if (rangeReadoutWidth > 0)
    {
        ana::ui::FixedGapRow topReadoutControls(topReadouts);
        if (mapMode)
        {
            frequencyHighControl.setBounds(
                topReadoutControls.takeLeft(frequencyReadoutWidth));
            rangeHighControl.setBounds({});
        }
        else
        {
            rangeHighControl.setBounds(topReadoutControls.takeLeft(levelReadoutWidth));
        }
    }
    else
    {
        if (! freqMode)
            frequencyHighControl.setBounds({});
        rangeHighControl.setBounds({});
    }

    if (freqMode)
    {
        frequencyLowControl.setBounds(plotBounds.getX(), readoutY,
                                      frequencyReadoutWidth, ana::ui::controlHeight);
        frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth
                                           - (showVerticalReadouts ? levelReadoutWidth + ana::ui::gap.pixels() : 0), readoutY,
                                       frequencyReadoutWidth, ana::ui::controlHeight);
        rangeLowControl.setBounds(graphRight - levelReadoutWidth, readoutY,
                                  levelReadoutWidth, ana::ui::controlHeight);
        mapTimeStartReadoutLabel.setBounds({});
        mapTimeEndReadoutLabel.setBounds({});
    }
    else if (mapMode)
    {
        frequencyLowControl.setBounds(graphRight - frequencyReadoutWidth, readoutY,
                                      frequencyReadoutWidth, ana::ui::controlHeight);
        rangeLowControl.setBounds({});
        if (offlineMap)
        {
            mapTimeStartReadoutLabel.setBounds(plotBounds.getX(), readoutY,
                                               timeReadoutWidth, ana::ui::controlHeight);
            mapTimeEndReadoutLabel.setBounds(graphRight - timeReadoutWidth
                                                 - (showVerticalReadouts ? frequencyReadoutWidth + ana::ui::gap.pixels() : 0),
                                             readoutY, timeReadoutWidth,
                                             ana::ui::controlHeight);
        }
        else
        {
            mapTimeStartReadoutLabel.setBounds({});
            mapTimeEndReadoutLabel.setBounds({});
        }
    }

    cursorReadoutLabel.setBounds({});
    cursorNoteReadoutLabel.setBounds({});
    cursorVerticalReadoutLabel.setBounds({});
    if ((showCursorHorizontal || showCursorNotes || showCursorVertical) && freqMode)
    {
        const auto left = frequencyLowControl.getRight() + ana::ui::gap.pixels();
        const auto right = frequencyHighControl.getX() - ana::ui::gap.pixels();
        const auto noteWidth = ana::ui::textControlWidth(4)
            - 2 * (ana::ui::textPadding - cursorNotePadding);
        const auto cursorBaseWidth = (showCursorHorizontal ? frequencyReadoutWidth : 0)
            + (showCursorNotes ? (showCursorHorizontal ? ana::ui::gap.pixels() : 0) + noteWidth : 0);
        const auto verticalFits = showCursorVertical && cursorBaseWidth
            + (cursorBaseWidth > 0 ? ana::ui::gap.pixels() : 0)
            + levelReadoutWidth <= right - left;
        const auto cursorWidth = cursorBaseWidth
            + (verticalFits ? (cursorBaseWidth > 0 ? ana::ui::gap.pixels() : 0) + levelReadoutWidth : 0);
        const auto cursorX = juce::jlimit(left, std::max(left, right - cursorWidth),
                                          plotBounds.getCentreX() - cursorWidth / 2);
        if (showCursorHorizontal)
            cursorReadoutLabel.setBounds(cursorX, readoutY,
                                         frequencyReadoutWidth, ana::ui::controlHeight);
        if (showCursorNotes)
            cursorNoteReadoutLabel.setBounds(cursorX
                                                + (showCursorHorizontal ? frequencyReadoutWidth + ana::ui::gap.pixels() : 0), readoutY,
                                             noteWidth, ana::ui::controlHeight);
        if (showCursorVertical)
            cursorVerticalReadoutLabel.setBounds(verticalFits
                                                     ? cursorX + cursorBaseWidth
                                                         + (cursorBaseWidth > 0 ? ana::ui::gap.pixels() : 0)
                                                     : graphRight - levelReadoutWidth,
                                                 verticalFits ? readoutY
                                                     : readoutY - ana::ui::controlHeight - ana::ui::gap.pixels(),
                                                 levelReadoutWidth, ana::ui::controlHeight);
    }
    else if ((showCursorHorizontal || showCursorNotes || showCursorVertical) && mapMode)
    {
        const auto left = showHorizontalReadouts
            ? mapTimeStartReadoutLabel.getRight() + ana::ui::gap.pixels()
            : plotBounds.getX();
        const auto right = showHorizontalReadouts
            ? mapTimeEndReadoutLabel.getX() - ana::ui::gap.pixels()
            : (showVerticalReadouts ? frequencyLowControl.getX() - ana::ui::gap.pixels() : graphRight);
        const auto noteWidth = ana::ui::textControlWidth(4)
            - 2 * (ana::ui::textPadding - cursorNotePadding);
        const auto frequencyWidth = (showCursorVertical ? mapCursorFrequencyWidth : 0)
            + (showCursorNotes ? (showCursorVertical ? ana::ui::gap.pixels() : 0) + noteWidth : 0);
        const auto timeFits = showCursorHorizontal && frequencyWidth
            + (frequencyWidth > 0 ? ana::ui::gap.pixels() : 0) + mapCursorTimeWidth <= right - left;
        const auto cursorWidth = frequencyWidth
            + (timeFits ? (frequencyWidth > 0 ? ana::ui::gap.pixels() : 0) + mapCursorTimeWidth : 0);
        const auto cursorX = juce::jlimit(left, std::max(left, right - cursorWidth),
                                        plotBounds.getCentreX() - cursorWidth / 2);
        if (showCursorVertical)
            cursorVerticalReadoutLabel.setBounds(cursorX, readoutY,
                mapCursorFrequencyWidth, ana::ui::controlHeight);
        if (showCursorNotes)
            cursorNoteReadoutLabel.setBounds(cursorX
                + (showCursorVertical ? mapCursorFrequencyWidth + ana::ui::gap.pixels() : 0),
                readoutY, noteWidth, ana::ui::controlHeight);
        if (showCursorHorizontal)
            cursorReadoutLabel.setBounds(timeFits
                ? cursorX + frequencyWidth + (frequencyWidth > 0 ? ana::ui::gap.pixels() : 0)
                : graphRight - mapCursorTimeWidth,
                timeFits ? readoutY : readoutY - ana::ui::controlHeight - ana::ui::gap.pixels(),
                mapCursorTimeWidth, ana::ui::controlHeight);
    }

    const auto horizontalZoomWidth = getWidth() - (showVerticalZoom
        ? bandZoomSliderWidth + ana::ui::gap.pixels() : 0);
    frequencyRangeSlider.setBounds(showHorizontalZoom && freqMode
        ? juce::Rectangle<int>(0, getHeight() - bandRangeSliderHeight,
                               horizontalZoomWidth, bandRangeSliderHeight)
        : juce::Rectangle<int>());
    mapTimeRangeSlider.setBounds(showHorizontalZoom && offlineMap
        ? juce::Rectangle<int>(0, getHeight() - bandRangeSliderHeight,
                               horizontalZoomWidth, bandRangeSliderHeight)
        : juce::Rectangle<int>());
    magnitudeRangeSlider.setBounds(showVerticalZoom
        ? juce::Rectangle<int>(getWidth() - bandZoomSliderWidth, 0,
                               bandZoomSliderWidth, getHeight())
        : juce::Rectangle<int>());

    if (mapMode)
    {
        {
            const auto analysisGeometryChanged = ! offlineSpectrogramImage.isValid()
                || offlineSpectrogramImage.getWidth() != plotBounds.getWidth()
                || offlineSpectrogramImage.getHeight() != plotBounds.getHeight();

            // Cache geometry follows the plot, but always spans the full source duration.
            if (analysisGeometryChanged
                && processor.getAnalyzerPageState() == ana::AnalyzerPage::spec)
                processor.requestOfflineAnalysis(getOfflineMapColumnCount(), false, true,
                                                 getOfflineMapRowCount());

            if (analysisGeometryChanged)
            {
                if (! offlineSpectrogramImage.isValid())
                    rebuildOfflineSpectrogramImage();
                else
                    scheduleMapImageRebuild();
            }
        }
    }

    syncRangeSliders();
    refreshControls();
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void OfflineSpecView::mouseMove(const juce::MouseEvent& event)
{
    if (processor.isCleanView())
    {
        cursorInside = false;
        return;
    }

    const auto nextCursorInside = getPlotBounds().contains(event.position);
    if (! nextCursorInside)
    {
        if (cursorInside)
        {
            cursorInside = false;
            repaint();
        }
        return;
    }

    if (cursorInside && cursorPosition == event.position)
        return;

    cursorInside = true;
    cursorPosition = event.position;
    updateCursorReadouts();
    repaint();
}

void OfflineSpecView::updateCursorReadouts()
{
    if ((cursorInside || lastCursorFrequency > 0.0f) && ! getPlotBounds().isEmpty())
    {
        const auto plotBounds = getPlotBounds();
        const auto frequencyRange = readSpecFrequencyRange(processor);
        const auto lowFrequency = frequencyRange.low;
        const auto highFrequency = frequencyRange.high;
        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
        const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);

        if (viewMode == ViewMode::map)
        {
            // In SPLIT, map cursor Y within the hovered pane's full frequency axis.
            const auto normalisedY = juce::jlimit(0.0f, 1.0f,
                (cursorPosition.y - cursorPane.getY()) / std::max(1.0f, cursorPane.getHeight()));
            lastCursorFrequency = ana::frequency_scale::frequencyAt(
                readSpecFrequencyScale(processor), lowFrequency, highFrequency,
                1.0f - normalisedY);
            cursorNoteReadoutLabel.setText(ana::ui::cursorNoteName(lastCursorFrequency), juce::dontSendNotification);
            cursorVerticalReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency),
                                               juce::dontSendNotification);

            const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
            if (const auto analysisResult = processor.getAnalysisResult();
                analysisResult != nullptr && analysisResult->durationSeconds > 0.0)
            {
                const auto visibleNormalised = mapTimeRangeStart
                    + normalisedX * (mapTimeRangeEnd - mapTimeRangeStart);
                const auto seconds = analysisResult->startTimeSeconds
                    + analysisResult->durationSeconds * visibleNormalised;
                cursorReadoutLabel.setText(formatMapTime(seconds), juce::dontSendNotification);
            }
            return;
        }

        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                                              (cursorPosition.x - plotBounds.getX())
                                                  / plotBounds.getWidth());
        lastCursorFrequency = ana::frequency_scale::frequencyAt(
            readSpecFrequencyScale(processor), lowFrequency, highFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        cursorNoteReadoutLabel.setText(ana::ui::cursorNoteName(lastCursorFrequency), juce::dontSendNotification);
        const auto displayRange = readSpecDisplayRange(processor);
        const auto lowRange = displayRange.low;
        const auto highRange = displayRange.high;
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - cursorPane.getY()) / std::max(1.0f, cursorPane.getHeight()));
        cursorVerticalReadoutLabel.setText(formatReadoutLevel(
                                               highRange - normalisedY * (highRange - lowRange)),
                                           juce::dontSendNotification);
    }
}

void OfflineSpecView::mouseExit(const juce::MouseEvent&)
{
    if (! cursorInside)
        return;

    cursorInside = false;
    repaint();
}

juce::Rectangle<float> OfflineSpecView::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    if (processor.isCleanView())
        return bounds;
    const auto* horizontalZoom = processor.getRawParameterValue(
        PluginProcessor::specHorizontalZoomParameterId);
    const auto* verticalZoom = processor.getRawParameterValue(
        PluginProcessor::specVerticalZoomParameterId);
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto showHorizontalZoom = (freqMode || mapMode) && (horizontalZoom == nullptr
        || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f);
    const auto showVerticalZoom = (freqMode || mapMode) && (verticalZoom == nullptr
        || verticalZoom->load(std::memory_order_relaxed) >= 0.5f);
    if (showHorizontalZoom)
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (showVerticalZoom)
        bounds.removeFromRight(static_cast<float>(bandZoomSliderWidth + ana::ui::gap.pixels()));
    return bounds;
}

size_t OfflineSpecView::getOfflineMapColumnCount() const noexcept
{
    // Time Overlap oversamples the plot-width base raster; it is not FFT-window overlap.
    return static_cast<size_t>(std::max(1,
        static_cast<int>(std::ceil(getPlotBounds().getWidth()))));
}

size_t OfflineSpecView::getOfflineMapRowCount() const noexcept
{
    // Keep analysis geometry independent of monitor mode. All channel variants are
    // calculated in one pass, so switching ST/LR/L/R/MS/M/S is render-only.
    const auto height = std::max(1, static_cast<int>(std::ceil(getPlotBounds().getHeight())));
    return static_cast<size_t>(std::max(
        static_cast<int>(ana::offline::SpectrogramMap::minimumRowCount), height));
}

void OfflineSpecView::timerCallback()
{
    if (!processor.isOfflineMode())
        return;
    const auto restoredViewMode = processor.isSpecMapView()
        ? ViewMode::map : ViewMode::frequency;
    if (restoredViewMode != viewMode)
        setViewMode(restoredViewMode);

    syncRangeSliders();
    refreshControls();

    const auto currentClearRevision = processor.getSpecProcessor().getClearRevision();
    if (displayedClearRevision != currentClearRevision)
    {
        displayedClearRevision = currentClearRevision;
    }

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);
    const auto rangeChanged = ! juce::approximatelyEqual(renderedMapRangeLow, lowRange)
        || ! juce::approximatelyEqual(renderedMapRangeHigh, highRange);
    const auto slopeChanged = ! juce::approximatelyEqual(renderedMapSlope, slope);
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto highQualityChanged = renderedMapHighQuality != (highQuality ? 1 : 0);
    const auto mapLeftToRight = readParameterValue(
        processor, PluginProcessor::specMapLeftToRightParameterId, 0.0f) >= 0.5f;
    const auto mapDirectionChanged = renderedMapLeftToRight != (mapLeftToRight ? 1 : 0);
    const auto frequencyScaleChanged = renderedMapFrequencyScale
        != static_cast<int>(frequencyScale);
    const auto colourMapChanged = renderedMapColourMap != static_cast<int>(colourMap);
    if (viewMode == ViewMode::map
        && (rangeChanged || slopeChanged || highQualityChanged || mapDirectionChanged
            || frequencyScaleChanged || colourMapChanged))
    {
        scheduleMapImageRebuild();
        renderedMapHighQuality = highQuality ? 1 : 0;
        renderedMapLeftToRight = mapLeftToRight ? 1 : 0;
        renderedMapFrequencyScale = static_cast<int>(frequencyScale);
        renderedMapColourMap = static_cast<int>(colourMap);
    }

    {
        if (processor.getAnalyzerPageState() != ana::AnalyzerPage::spec)
            return;

        if (viewMode == ViewMode::map)
        {
            // Keep a full-duration cache; horizontal time zoom remains render-only.
            processor.requestOfflineAnalysis(getOfflineMapColumnCount(), false, true,
                                             getOfflineMapRowCount());

            if (updateOfflineRevision(processor, displayedOfflineRevision))
            {
                scheduleMapImageRebuild();
                repaint();
            }

            const auto useSplit = ana::spec::supportsSplitView(monitorMode);
            if (renderedOfflineMonitorMode != ana::spec::monitorModeIndex(monitorMode)
                || renderedOfflineSplitView != useSplit)
                scheduleMapImageRebuild();

            if (mapImageRebuildPending)
            {
                mapImageRebuildPending = false;
                rebuildOfflineSpectrogramImage();
            }
        }
        else
        {
            const auto firstGraphType = static_cast<int>(readSpecDisplayType(
                processor, PluginProcessor::specFirstGraphTypeParameterId));
            if (displayedFirstGraphType != firstGraphType)
            {
                displayedFirstGraphType = firstGraphType;
                repaint();
            }
            processor.requestOfflineAnalysis(size_t { 1 });
            if (updateOfflineRevision(processor, displayedOfflineRevision))
            {
                repaint();
            }
        }
        return;
    }
}

void OfflineSpecView::refreshControls()
{
    freqButton.setVisible(true);
    mapButton.setVisible(true);
    const auto readValue = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto offlineMap = mapMode;
    const auto showMonitor = readValue(PluginProcessor::specMonitorControlsParameterId, 1.0f) >= 0.5f;
    const auto showHorizontalZoom = readValue(PluginProcessor::specHorizontalZoomParameterId, 1.0f) >= 0.5f;
    const auto showVerticalZoom = readValue(PluginProcessor::specVerticalZoomParameterId, 1.0f) >= 0.5f;
    const auto showHorizontalReadouts = readValue(PluginProcessor::specHorizontalReadoutsParameterId, 1.0f) >= 0.5f;
    const auto showVerticalReadouts = readValue(PluginProcessor::specVerticalReadoutsParameterId, 1.0f) >= 0.5f;
    const auto showCursorHorizontal = readValue(PluginProcessor::specCursorReadoutParameterId, 1.0f) >= 0.5f;
    const auto showCursorNotes = readValue(PluginProcessor::specCursorNotesParameterId, 1.0f) >= 0.5f;
    const auto showCursorVertical = readValue(PluginProcessor::specCursorVerticalReadoutParameterId, 1.0f) >= 0.5f;
    freqButton.setToggleState(freqMode, juce::dontSendNotification);
    mapButton.setToggleState(mapMode, juce::dontSendNotification);
    const auto monitorMode = readSpecMonitorMode(processor);
    const auto modeIndex = ana::spec::monitorModeIndex(monitorMode);
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor);
        monitorButtons[index]->setToggleState(static_cast<int>(index) == modeIndex, juce::dontSendNotification);
    }
    frequencyRangeSlider.setVisible(showHorizontalZoom && freqMode);
    mapTimeRangeSlider.setVisible(showHorizontalZoom && offlineMap);
    magnitudeRangeSlider.setVisible(showVerticalZoom && (freqMode || mapMode));

    frequencyLowControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    frequencyHighControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    rangeLowControl.setVisible(freqMode && showVerticalReadouts);
    rangeHighControl.setVisible(freqMode && showVerticalReadouts);
    mapTimeStartReadoutLabel.setVisible(offlineMap && showHorizontalReadouts);
    mapTimeEndReadoutLabel.setVisible(offlineMap && showHorizontalReadouts);
    cursorReadoutLabel.setVisible((freqMode || mapMode) && showCursorHorizontal);
    cursorNoteReadoutLabel.setVisible(showCursorNotes);
    cursorVerticalReadoutLabel.setVisible((freqMode || mapMode) && showCursorVertical);
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void OfflineSpecView::setViewMode(const ViewMode nextViewMode)
{
    if (viewMode == nextViewMode)
        return;

    viewMode = nextViewMode;
    processor.setSpecMapView(viewMode == ViewMode::map);
    cursorInside = false;
    if (viewMode == ViewMode::map)
    {
        if (processor.getAnalyzerPageState() == ana::AnalyzerPage::spec)
            processor.requestOfflineAnalysis(getOfflineMapColumnCount(), false, true,
                                             getOfflineMapRowCount());
        scheduleMapImageRebuild();
    }
    resized();
    repaint();
}

void OfflineSpecView::scheduleMapImageRebuild() noexcept
{
    mapImageRebuildPending = true;
}

void OfflineSpecView::rebuildOfflineSpectrogramImage()
{
    if (viewMode != ViewMode::map)
        return;

    // Preserve the last completed raster across REAPER editor-peer recreation.
    if (! offlineSpectrogramImage.isValid())
        offlineSpectrogramImage = processor.getCachedOfflineSpectrogramImage();

    const auto analysisResult = processor.getAnalysisResult();

    if (analysisResult != nullptr && analysisResult->selectionEmpty)
    {
        const auto bounds = getPlotBounds().toNearestInt();
        offlineSpectrogramImage = juce::Image(juce::Image::ARGB,
            std::max(1, bounds.getWidth()), std::max(1, bounds.getHeight()), true);
        offlineSpectrogramImage.clear(offlineSpectrogramImage.getBounds(), ana::ui::background);
        processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
        const auto mode = readSpecMonitorMode(processor);
        renderedOfflineMonitorMode = ana::spec::monitorModeIndex(mode);
        renderedOfflineSplitView = ana::spec::supportsSplitView(mode);
        repaint();
        return;
    }

    // Host/ARA churn must not replace the last valid raster with an empty intermediate result.
    if (analysisResult == nullptr || analysisResult->specMap == nullptr || ! analysisResult->specMap->isValid())
        return;

    const auto bounds = getPlotBounds().toNearestInt();
    const auto width = std::max(1, bounds.getWidth());
    const auto height = std::max(1, bounds.getHeight());
    // Commit a replacement raster only after it is fully rendered.
    juce::Image nextSpectrogramImage(juce::Image::ARGB, width, height, true);
    nextSpectrogramImage.clear(
        nextSpectrogramImage.getBounds(),
        ana::ui::background);

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);
    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = highQuality ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);

    const auto& map = *analysisResult->specMap;
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto firstIndex = ana::spec::channelIndex(firstChannel);
    const auto secondIndex = ana::spec::channelIndex(secondChannel);
    const auto useSplitView = ana::spec::supportsSplitView(monitorMode);
    const auto useSecondSpectrum = secondChannel != firstChannel && ! useSplitView;
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto colourForValue = [&] (const float value, const float frequency)
    {
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto displayValue = value + slopeOffset;
        return spectrogramColour(juce::jlimit(0.0f, 1.0f,
            (displayValue - lowRange) / (highRange - lowRange)), colourMap);
    };

    const auto mapAt = [&map] (const size_t channel, const size_t row, const size_t column)
    {
        return map.levels[channel][row * map.columnCount + column];
    };

    // Use the native raster only for an exact geometry/range match.
    const auto paneHeight = useSplitView ? std::max(1, height / 2) : height;
    const auto allColumnsPopulated = std::all_of(
        map.columnFrameCounts.begin(), map.columnFrameCounts.end(),
        [] (const uint32_t count) { return count != 0; });
    const auto nativeRaster = allColumnsPopulated
        && map.timeSpans.size() == 1
        && map.timeSpans.front().start <= 0.0 && map.timeSpans.front().end >= 1.0
        && firstIndex != 0 && secondIndex != 0
        && map.columnCount == static_cast<size_t>(width)
        && map.rowCount == static_cast<size_t>(paneHeight)
        && (! useSplitView || height % 2 == 0)
        && juce::approximatelyEqual(mapTimeRangeStart, 0.0f)
        && juce::approximatelyEqual(mapTimeRangeEnd, 1.0f)
        && frequencyScale == ana::frequency_scale::Scale::logarithmic
        && juce::approximatelyEqual(lowFrequency, ana::frequency_scale::minimumHz)
        && juce::approximatelyEqual(highFrequency, ana::frequency_scale::maximumHz);

    // HQ always uses max-bilinear sampling for both split panes; native blits are non-HQ only.
    if (nativeRaster && ! highQuality)
    {
        juce::Image::BitmapData pixels(nextSpectrogramImage, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
        {
            const auto secondHalf = useSplitView && y >= paneHeight;
            const auto localY = secondHalf ? y - paneHeight : y;
            const auto row = static_cast<size_t>(juce::jlimit(0, paneHeight - 1, localY));
            const auto channel = secondHalf ? secondIndex : firstIndex;
            const auto centreNormalised = 1.0f
                - (static_cast<float>(localY) + 0.5f) / static_cast<float>(paneHeight);
            const auto centreFrequency = ana::frequency_scale::frequencyAt(
                frequencyScale, lowFrequency, highFrequency, centreNormalised);
            for (int x = 0; x < width; ++x)
            {
                auto value = mapAt(channel, row, static_cast<size_t>(x));
                if (! useSplitView && useSecondSpectrum)
                    value = std::max(value,
                        mapAt(secondIndex, row, static_cast<size_t>(x)));
                pixels.setPixelColour(x, y, colourForValue(value, centreFrequency));
            }
        }

        offlineSpectrogramImage = std::move(nextSpectrogramImage);
        processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
        renderedOfflineMonitorMode = ana::spec::monitorModeIndex(monitorMode);
        renderedOfflineSplitView = useSplitView;
        repaint();
        return;
    }

    struct FrequencyLookup
    {
        size_t row0 = 0;
        size_t row1 = 0;
        float rowMix = 0.0f;
        size_t rowPeakFirst = 0;
        size_t rowPeakLast = 0;
        bool rowUsePeak = false;
        size_t fftBin0 = 0;
        size_t fftBin1 = 0;
        float fftBinMix = 0.0f;
        size_t fftPeakFirst = 0;
        size_t fftPeakLast = 0;
        bool fftUsePeak = false;
        float centreFrequency = 0.0f;
    };
    struct TimeLookup
    {
        size_t column0 = 0;
        size_t column1 = 0;
        float mix = 0.0f;
        size_t peakFirst = 0;
        size_t peakLast = 0;
        size_t spanFirst = 0;
        size_t spanLast = 0;
        bool usePeak = false;
        bool valid = false;
    };

    std::vector<FrequencyLookup> frequencyForY(static_cast<size_t>(height));
    std::vector<bool> useSecondChannelForY(static_cast<size_t>(height), false);

    const auto sourceRowForFrequency = [&map] (const float frequency)
    {
        const auto clampedFrequency = juce::jlimit(
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz,
            frequency);
        const auto normalised = ana::spectrogram_frequency::normalisedForFrequency(
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz,
            clampedFrequency);
        return juce::jlimit(0.0f, static_cast<float>(map.rowCount - 1),
            (1.0f - normalised) * static_cast<float>(map.rowCount) - 0.5f);
    };

    const auto makeFrequencyLookup = [&] (const int localY, const int localHeight)
    {
        FrequencyLookup lookup;
        const auto safeHeight = std::max(1, localHeight);
        const auto invHeight = 1.0f / static_cast<float>(safeHeight);
        const auto y = static_cast<float>(juce::jlimit(0, safeHeight - 1, localY));
        const auto topNormalised = 1.0f - y * invHeight;
        const auto bottomNormalised = 1.0f - (y + 1.0f) * invHeight;
        const auto centreNormalised = 1.0f - (y + 0.5f) * invHeight;
        const auto centreFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, centreNormalised);
        const auto upperFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, topNormalised);
        const auto lowerFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, bottomNormalised);
        lookup.centreFrequency = centreFrequency;

        const auto exactRow = sourceRowForFrequency(centreFrequency);
        lookup.row0 = static_cast<size_t>(std::floor(exactRow));
        lookup.row1 = std::min(map.rowCount - 1, lookup.row0 + 1);
        lookup.rowMix = exactRow - static_cast<float>(lookup.row0);

        const auto upperRow = sourceRowForFrequency(upperFrequency);
        const auto lowerRow = sourceRowForFrequency(lowerFrequency);
        const auto firstRowCoord = std::min(upperRow, lowerRow);
        const auto lastRowCoord = std::max(upperRow, lowerRow);
        if (lastRowCoord - firstRowCoord > 1.0f)
        {
            const auto first = std::max(0, static_cast<int>(std::ceil(firstRowCoord)));
            const auto last = std::min(static_cast<int>(map.rowCount - 1),
                                       static_cast<int>(std::floor(lastRowCoord)));
            if (first <= last)
            {
                lookup.rowPeakFirst = static_cast<size_t>(first);
                lookup.rowPeakLast = static_cast<size_t>(last);
                lookup.rowUsePeak = true;
            }
        }

        // Track each destination pixel's source-bin footprint for max-bilinear downsampling.
        const auto binFrequency = map.fftSize > 0
            ? static_cast<float>(map.sampleRate) / static_cast<float>(map.fftSize)
            : 0.0f;
        if (map.binCount >= 2 && binFrequency > 0.0f)
        {
            const auto clampBin = [&] (const float frequency)
            {
                return juce::jlimit(1.0f, static_cast<float>(map.binCount - 1),
                                    frequency / binFrequency);
            };
            const auto exactBin = clampBin(centreFrequency);
            lookup.fftBin0 = static_cast<size_t>(std::floor(exactBin));
            lookup.fftBin1 = std::min(map.binCount - 1, lookup.fftBin0 + 1);
            lookup.fftBinMix = exactBin - static_cast<float>(lookup.fftBin0);

            const auto upperBin = clampBin(upperFrequency);
            const auto lowerBin = clampBin(lowerFrequency);
            const auto firstBinCoord = std::min(upperBin, lowerBin);
            const auto lastBinCoord = std::max(upperBin, lowerBin);
            if (lastBinCoord - firstBinCoord > 1.0f)
            {
                const auto first = std::max(1, static_cast<int>(std::ceil(firstBinCoord)));
                const auto last = std::min(static_cast<int>(map.binCount - 1),
                                           static_cast<int>(std::floor(lastBinCoord)));
                if (first <= last)
                {
                    lookup.fftPeakFirst = static_cast<size_t>(first);
                    lookup.fftPeakLast = static_cast<size_t>(last);
                    lookup.fftUsePeak = true;
                }
            }
        }

        return lookup;
    };

    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto yi = static_cast<size_t>(y);
        frequencyForY[yi] = makeFrequencyLookup(localY, localHeight);
        useSecondChannelForY[yi] = secondHalf;
    }

    std::vector<TimeLookup> timeForX(static_cast<size_t>(width));
    const ana::offline::SpectrogramTimeline timeline(map.columnFrameCounts, map.timeSpans);

    const auto framePositionForTime = [&map] (const float normalisedTime)
    {
        // Cached columns are pixel-centred at (i + 0.5) / N.
        return juce::jlimit(0.0f, static_cast<float>(map.columnCount - 1),
            juce::jlimit(0.0f, 1.0f, normalisedTime)
                * static_cast<float>(map.columnCount) - 0.5f);
    };

    const auto findTimeBracket = [&timeline] (const float normalisedTime)
    {
        TimeLookup lookup;
        const auto bracket = timeline.bracket(normalisedTime);
        lookup.column0 = bracket.column0;
        lookup.column1 = bracket.column1;
        lookup.mix = bracket.mix;
        lookup.spanFirst = bracket.spanFirst;
        lookup.spanLast = bracket.spanLast;
        lookup.valid = bracket.valid;
        return lookup;
    };

    for (int x = 0; x < width; ++x)
    {
        // Max-bilinear downsampling preserves the strongest measured cell in each destination pixel.
        const auto invWidth = 1.0f / static_cast<float>(std::max(1, width));
        const auto centreLocal = (static_cast<float>(x) + 0.5f) * invWidth;
        const auto leftLocal = static_cast<float>(x) * invWidth;
        const auto rightLocal = static_cast<float>(x + 1) * invWidth;
        const auto toMapTime = [&] (const float local)
        {
            return juce::jlimit(0.0f, 1.0f,
                mapTimeRangeStart + (mapTimeRangeEnd - mapTimeRangeStart) * local);
        };

        auto lookup = findTimeBracket(toMapTime(centreLocal));
        const auto firstPosition = framePositionForTime(toMapTime(leftLocal));
        const auto lastPosition = framePositionForTime(toMapTime(rightLocal));
        const auto firstCoord = std::min(firstPosition, lastPosition);
        const auto lastCoord = std::max(firstPosition, lastPosition);
        if (lastCoord - firstCoord > 1.0f)
        {
            const auto first = std::max(static_cast<int>(lookup.spanFirst),
                                        static_cast<int>(std::ceil(firstCoord)));
            const auto last = std::min(static_cast<int>(lookup.spanLast),
                                       static_cast<int>(std::floor(lastCoord)));
            if (first <= last)
            {
                lookup.peakFirst = static_cast<size_t>(first);
                lookup.peakLast = static_cast<size_t>(last);
                lookup.usePeak = true;
            }
        }
        timeForX[static_cast<size_t>(x)] = lookup;
    }

    const auto atRow = [&map] (const size_t channel, const size_t row, const size_t column)
    {
        return map.levels[channel][row * map.columnCount + column];
    };
    const auto atStereoBin = [&map] (const size_t bin, const size_t column)
    {
        return map.stereoDecibels[column * map.binCount + bin];
    };
    const auto sampleHighQuality = [&] (const size_t channel,
                                         const FrequencyLookup& frequency,
                                         const TimeLookup& time)
    {
        // Interpolate in dB space and retain the local maximum when downsampling.
        if (channel == 0 && map.binCount >= 2
            && map.stereoDecibels.size() == map.columnCount * map.binCount)
        {
            const auto first0 = atStereoBin(frequency.fftBin0, time.column0);
            const auto first1 = atStereoBin(frequency.fftBin1, time.column0);
            const auto second0 = atStereoBin(frequency.fftBin0, time.column1);
            const auto second1 = atStereoBin(frequency.fftBin1, time.column1);
            const auto first = first0
                + frequency.fftBinMix * (first1 - first0);
            const auto second = second0
                + frequency.fftBinMix * (second1 - second0);
            auto value = first + time.mix * (second - first);

            if (highQuality && (frequency.fftUsePeak || time.usePeak))
            {
                const auto firstBin = frequency.fftUsePeak
                    ? frequency.fftPeakFirst : frequency.fftBin0;
                const auto lastBin = frequency.fftUsePeak
                    ? frequency.fftPeakLast : frequency.fftBin1;
                const auto firstColumn = time.usePeak ? time.peakFirst : time.column0;
                const auto lastColumn = time.usePeak ? time.peakLast : time.column1;
                auto peak = ana::spec::SpecProcessor::minimumDecibels;
                for (size_t bin = firstBin; bin <= lastBin; ++bin)
                    for (size_t column = firstColumn; column <= lastColumn; ++column)
                        if (map.columnFrameCounts[column] != 0)
                            peak = std::max(peak, atStereoBin(bin, column));
                value = std::max(value, peak);
            }
            return value;
        }

        const auto v00 = atRow(channel, frequency.row0, time.column0);
        const auto v01 = atRow(channel, frequency.row0, time.column1);
        const auto v10 = atRow(channel, frequency.row1, time.column0);
        const auto v11 = atRow(channel, frequency.row1, time.column1);
        const auto v0 = v00 + time.mix * (v01 - v00);
        const auto v1 = v10 + time.mix * (v11 - v10);
        auto value = v0 + frequency.rowMix * (v1 - v0);

        if (highQuality && (frequency.rowUsePeak || time.usePeak))
        {
            const auto firstRow = frequency.rowUsePeak
                ? frequency.rowPeakFirst : frequency.row0;
            const auto lastRow = frequency.rowUsePeak
                ? frequency.rowPeakLast : frequency.row1;
            const auto firstColumn = time.usePeak ? time.peakFirst : time.column0;
            const auto lastColumn = time.usePeak ? time.peakLast : time.column1;
            auto peak = ana::spec::SpecProcessor::minimumDecibels;
            for (size_t row = firstRow; row <= lastRow; ++row)
                for (size_t column = firstColumn; column <= lastColumn; ++column)
                    if (map.columnFrameCounts[column] != 0)
                        peak = std::max(peak, atRow(channel, row, column));
            value = std::max(value, peak);
        }
        return value;
    };

    juce::Image::BitmapData pixels(nextSpectrogramImage, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < width; ++x)
    {
        const auto& time = timeForX[static_cast<size_t>(x)];
        if (! time.valid)
            continue;
        for (int y = 0; y < height; ++y)
        {
            const auto yi = static_cast<size_t>(y);
            const auto channel = useSecondChannelForY[yi] ? secondIndex : firstIndex;
            auto value = sampleHighQuality(channel, frequencyForY[yi], time);
            if (! useSplitView && useSecondSpectrum)
                value = std::max(value,
                    sampleHighQuality(secondIndex, frequencyForY[yi], time));
            pixels.setPixelColour(x, y, colourForValue(value, frequencyForY[yi].centreFrequency));
        }
    }

    offlineSpectrogramImage = std::move(nextSpectrogramImage);
    processor.setCachedOfflineSpectrogramImage(offlineSpectrogramImage);
    renderedOfflineMonitorMode = ana::spec::monitorModeIndex(monitorMode);
    renderedOfflineSplitView = useSplitView;
    repaint();
}

void OfflineSpecView::syncRangeSliders()
{
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto* horizontalReadouts = processor.getRawParameterValue(
        PluginProcessor::specHorizontalReadoutsParameterId);
    const auto* verticalReadouts = processor.getRawParameterValue(
        PluginProcessor::specVerticalReadoutsParameterId);
    const auto* cursorReadout = processor.getRawParameterValue(
        PluginProcessor::specCursorReadoutParameterId);
    const auto* cursorVerticalReadout = processor.getRawParameterValue(
        PluginProcessor::specCursorVerticalReadoutParameterId);
    const auto* cursorNotes = processor.getRawParameterValue(
        PluginProcessor::specCursorNotesParameterId);
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto showHorizontalReadouts = horizontalReadouts == nullptr
        || horizontalReadouts->load(std::memory_order_relaxed) >= 0.5f;
    const auto showVerticalReadouts = verticalReadouts == nullptr
        || verticalReadouts->load(std::memory_order_relaxed) >= 0.5f;
    const auto shouldShowHorizontalCursor = (freqMode || mapMode)
        && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);
    const auto shouldShowVerticalCursor = (freqMode || mapMode)
        && (cursorVerticalReadout == nullptr || cursorVerticalReadout->load(std::memory_order_relaxed) >= 0.5f);

    frequencyLowControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    frequencyHighControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    rangeLowControl.setVisible(freqMode && showVerticalReadouts);
    rangeHighControl.setVisible(freqMode && showVerticalReadouts);
    cursorReadoutLabel.setVisible(shouldShowHorizontalCursor);
    cursorNoteReadoutLabel.setVisible((cursorNotes == nullptr || cursorNotes->load(std::memory_order_relaxed) >= 0.5f));
    cursorVerticalReadoutLabel.setVisible(shouldShowVerticalCursor);
    mapTimeStartReadoutLabel.setVisible(mapMode && showHorizontalReadouts);
    mapTimeEndReadoutLabel.setVisible(mapMode && showHorizontalReadouts);
    updateCursorReadouts();
    updateMapTimeReadouts();

    mapTimeRangeStart = readParameterValue(
        processor, PluginProcessor::specMapTimeRangeStartParameterId, 0.0f);
    mapTimeRangeEnd = readParameterValue(
        processor, PluginProcessor::specMapTimeRangeEndParameterId, 1.0f);
    if (mapTimeRangeEnd < mapTimeRangeStart)
        std::swap(mapTimeRangeStart, mapTimeRangeEnd);

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, lowFrequency),
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, highFrequency));
    mapTimeRangeSlider.setRange(mapTimeRangeStart, mapTimeRangeEnd);
    if (mapMode)
    {
        const auto lower = ana::frequency_scale::normalisedForFrequency(
            frequencyScale,
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, lowFrequency);
        const auto upper = ana::frequency_scale::normalisedForFrequency(
            frequencyScale,
            ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, highFrequency);
        magnitudeRangeSlider.setRange(1.0f - upper, 1.0f - lower);
    }
    else
    {
        constexpr auto magnitudeSpan = ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplayDecibels;
        magnitudeRangeSlider.setRange((ana::spec::maximumDisplayDecibels - std::max(lowRange, highRange)) / magnitudeSpan,
                                      (ana::spec::maximumDisplayDecibels - std::min(lowRange, highRange)) / magnitudeSpan);
    }
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void OfflineSpecView::updateFrequencyRangeFromSlider()
{
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto lowFrequency = ana::frequency_scale::frequencyAt(
        frequencyScale, ana::frequency_scale::minimumHz,
        ana::frequency_scale::maximumHz, frequencyRangeSlider.getRangeStart());
    const auto highFrequency = ana::frequency_scale::frequencyAt(
        frequencyScale, ana::frequency_scale::minimumHz,
        ana::frequency_scale::maximumHz, frequencyRangeSlider.getRangeEnd());
    frequencyLowControl.getSlider().setValue(lowFrequency, juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(highFrequency, juce::sendNotificationSync);
    repaint();
}

void OfflineSpecView::updateVerticalRangeFromSlider()
{
    if (viewMode == ViewMode::map)
    {
        const auto frequencyScale = readSpecFrequencyScale(processor);
        const auto highFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, 1.0f - magnitudeRangeSlider.getRangeStart());
        const auto lowFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, 1.0f - magnitudeRangeSlider.getRangeEnd());
        frequencyLowControl.getSlider().setValue(lowFrequency, juce::sendNotificationSync);
        frequencyHighControl.getSlider().setValue(highFrequency, juce::sendNotificationSync);

        scheduleMapImageRebuild();
        repaint();
        return;
    }

    constexpr auto span = ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplayDecibels;
    const auto highRange = ana::spec::maximumDisplayDecibels - magnitudeRangeSlider.getRangeStart() * span;
    const auto lowRange = ana::spec::maximumDisplayDecibels - magnitudeRangeSlider.getRangeEnd() * span;
    rangeLowControl.getSlider().setValue(lowRange, juce::sendNotificationSync);
    rangeHighControl.getSlider().setValue(highRange, juce::sendNotificationSync);

    repaint();
}

void OfflineSpecView::updateMapTimeRangeFromSlider()
{
    mapTimeRangeStart = mapTimeRangeSlider.getRangeStart();
    mapTimeRangeEnd = mapTimeRangeSlider.getRangeEnd();
    setParameterPlainValue(processor, PluginProcessor::specMapTimeRangeStartParameterId,
                           mapTimeRangeStart);
    setParameterPlainValue(processor, PluginProcessor::specMapTimeRangeEndParameterId,
                           mapTimeRangeEnd);
    updateMapTimeReadouts();

    scheduleMapImageRebuild();
    repaint();
}

void OfflineSpecView::updateMapTimeReadouts()
{
    const auto analysisResult = processor.getAnalysisResult();
    if (analysisResult == nullptr || analysisResult->durationSeconds <= 0.0)
    {
        mapTimeStartReadoutLabel.setText("00:00.000", juce::dontSendNotification);
        mapTimeEndReadoutLabel.setText("00:00.000", juce::dontSendNotification);
        return;
    }
    mapTimeStartReadoutLabel.setText(
        formatMapTime(analysisResult->startTimeSeconds + analysisResult->durationSeconds * mapTimeRangeStart),
        juce::dontSendNotification);
    mapTimeEndReadoutLabel.setText(
        formatMapTime(analysisResult->startTimeSeconds + analysisResult->durationSeconds * mapTimeRangeEnd),
        juce::dontSendNotification);
}
