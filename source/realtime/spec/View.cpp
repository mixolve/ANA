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

bool shouldUseSplitView(const PluginProcessor& processor,
                        const ana::spec::MonitorMode mode,
                        const bool mapMode) noexcept
{
    if (! ana::spec::supportsSplitView(mode))
        return false;

    if (mapMode)
        return true;

    const auto* split = processor.getRawParameterValue(
        PluginProcessor::specSplitViewParameterId);
    return split != nullptr && split->load(std::memory_order_relaxed) >= 0.5f;
}

float sampleMapSpectrum(const std::vector<float>& spectrum,
                          const float binFrequency,
                          const float lowerFrequency,
                          const float centreFrequency,
                          const float upperFrequency,
                          const bool highQuality) noexcept
{
    constexpr float floorDb = ana::spec::SpecProcessor::minimumDecibels;
    if (spectrum.size() < 2 || binFrequency <= 0.0f)
        return floorDb;

    const auto maxBin = static_cast<int>(spectrum.size()) - 1;
    const auto clampBin = [maxBin, binFrequency] (const float frequency)
    {
        return juce::jlimit(1.0f, static_cast<float>(maxBin), frequency / binFrequency);
    };
    const auto exactBin = clampBin(centreFrequency);
    const auto bin0 = static_cast<int>(std::floor(exactBin));
    const auto bin1 = std::min(maxBin, bin0 + 1);
    const auto mix = exactBin - static_cast<float>(bin0);
    auto value = spectrum[static_cast<size_t>(bin0)]
        + mix * (spectrum[static_cast<size_t>(bin1)] - spectrum[static_cast<size_t>(bin0)]);

    if (! highQuality)
        return value;

    // Max-bilinear sampling preserves narrow peaks when source bins collapse into one row.
    const auto firstCoord = std::min(clampBin(lowerFrequency), clampBin(upperFrequency));
    const auto lastCoord = std::max(clampBin(lowerFrequency), clampBin(upperFrequency));
    const auto first = std::max(1, static_cast<int>(std::ceil(firstCoord)));
    const auto last = std::min(maxBin, static_cast<int>(std::floor(lastCoord)));
    for (auto bin = first; bin <= last; ++bin)
        value = std::max(value, spectrum[static_cast<size_t>(bin)]);
    return value;
}
}

RealtimeSpecView::RealtimeSpecView(PluginProcessor& processorRef,
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

    configureCursorReadoutLabel(cursorReadoutLabel);
    addAndMakeVisible(cursorReadoutLabel);

    configureCursorReadoutLabel(cursorNoteReadoutLabel);
    cursorNoteReadoutLabel.setBorderSize(
        juce::BorderSize<int>(1, cursorNotePadding, 1, cursorNotePadding));
    addAndMakeVisible(cursorNoteReadoutLabel);

    configureCursorReadoutLabel(cursorVerticalReadoutLabel);
    addAndMakeVisible(cursorVerticalReadoutLabel);

    viewMode = processor.isSpecMapView() ? ViewMode::map : ViewMode::frequency;
    processor.getSpecProcessor().setRealtimeMapMode(viewMode == ViewMode::map);
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
    splitButton.setTooltip("SPLIT");
    splitButton.setClickingTogglesState(true);
    splitButton.onClick = [this]
    {
        if (viewMode != ViewMode::frequency)
            return;

        if (auto* parameter = processor.getActiveParameter(
                PluginProcessor::specSplitViewParameterId))
            parameter->setValueNotifyingHost(splitButton.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(splitButton);

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
            clearSpectrogram();
        }
        repaint();
    };
    frequencyHighControl.onValueChanged = [this]
    {
        if (viewMode == ViewMode::map)
        {
            clearSpectrogram();
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
    magnitudeRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateVerticalRangeFromSlider();
    };

    startTimerHz(60);
}

bool RealtimeSpecView::captureSnapshot(const size_t snapshotIndex)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    ana::spec::SpectrumSnapshot captured;
    captured.colour = snapshots[snapshotIndex].colour;
    captured.gainDb = snapshots[snapshotIndex].gainDb;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, false);
    const auto* secondGraphEnabled = processor.getRawParameterValue(
        PluginProcessor::specSecondGraphParameterId);
    captured.drawSecondGraph = useSplitView || secondGraphEnabled == nullptr
        || secondGraphEnabled->load(std::memory_order_relaxed) >= 0.5f;

    const auto* displayedSpec = &processor.getSpecProcessor();
    captured.sampleRate = displayedSpec->getSampleRate();

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

void RealtimeSpecView::clearSpectrogram()
{
    realtimeMapWriteColumn = 0;
    realtimeMapColumnAccumulator = 0.0;
    realtimeMapLastAdvanceMilliseconds = 0.0;
    std::fill(realtimeSpectrogramLevels.begin(), realtimeSpectrogramLevels.end(), ana::spec::SpecProcessor::minimumDecibels);
    if (spectrogramImage.isValid())
        spectrogramImage.clear(spectrogramImage.getBounds(), ana::ui::background);
    repaint();
}

void RealtimeSpecView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::background);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    if (viewMode == ViewMode::map)
    {
        const auto& image = spectrogramImage;
        if (image.isValid())
            graphics.drawImage(image, plotBounds);

        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);
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
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, false);
    const auto* secondGraphEnabled = processor.getRawParameterValue(
        PluginProcessor::specSecondGraphParameterId);
    const auto drawSecondGraph = useSplitView
        || secondGraphEnabled == nullptr
        || secondGraphEnabled->load(std::memory_order_relaxed) >= 0.5f;
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
    copySpectra(processor.getSpecProcessor());

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

void RealtimeSpecView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto readVisibility = [this] (const char* parameterId)
    {
        const auto* value = processor.getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto showCursorHorizontal = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorReadoutParameterId);
    const auto showCursorNotes = readVisibility(PluginProcessor::specCursorNotesParameterId);
    const auto showCursorVertical = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorVerticalReadoutParameterId);
    const auto showMonitor = readVisibility(PluginProcessor::specMonitorControlsParameterId);
    const auto showHorizontalZoom = freqMode
        && readVisibility(PluginProcessor::specHorizontalZoomParameterId);
    const auto showVerticalZoom = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specVerticalZoomParameterId);
    const auto showVerticalReadouts = readVisibility(PluginProcessor::specVerticalReadoutsParameterId);
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
    if (freqMode)
        monitorWidth += splitButton.getPreferredWidth() + ana::ui::gap.pixels();
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
    splitButton.setBounds({});
    if (showMonitor && freqMode)
        placeMonitorControl(splitButton, splitButton.getPreferredWidth());

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
    }
    else if (mapMode)
    {
        frequencyLowControl.setBounds(graphRight - frequencyReadoutWidth, readoutY,
                                      frequencyReadoutWidth, ana::ui::controlHeight);
        rangeLowControl.setBounds({});
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
        const auto right = frequencyLowControl.getX() - ana::ui::gap.pixels();
        const auto left = plotBounds.getX();
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
    magnitudeRangeSlider.setBounds(showVerticalZoom
        ? juce::Rectangle<int>(getWidth() - bandZoomSliderWidth, 0,
                               bandZoomSliderWidth, getHeight())
        : juce::Rectangle<int>());

    if (mapMode)
    {
        resetSpectrogramImage();
    }

    syncRangeSliders();
    refreshControls();
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void RealtimeSpecView::mouseDown(const juce::MouseEvent& event)
{
    const auto* resetClick = processor.getRawParameterValue(
        PluginProcessor::specResetClickParameterId);
    if (event.originalComponent != this || processor.getSpecProcessor().isFrozen()
        || resetClick == nullptr || resetClick->load(std::memory_order_relaxed) < 0.5f)
        return;

    processor.clearSpecProcessor();
    if (viewMode == ViewMode::map)
        clearSpectrogram();
}

void RealtimeSpecView::mouseMove(const juce::MouseEvent& event)
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

void RealtimeSpecView::updateCursorReadouts()
{
    if ((cursorInside || lastCursorFrequency > 0.0f) && ! getPlotBounds().isEmpty())
    {
        const auto plotBounds = getPlotBounds();
        const auto frequencyRange = readSpecFrequencyRange(processor);
        const auto lowFrequency = frequencyRange.low;
        const auto highFrequency = frequencyRange.high;
        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = shouldUseSplitView(
            processor, monitorMode, viewMode == ViewMode::map);
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
            {
                const auto ageSeconds = (1.0 - static_cast<double>(normalisedX))
                    * processor.getSpecMapTimeMilliseconds() * 0.001;
                cursorReadoutLabel.setText(ageSeconds > 0.0005
                                               ? "-" + formatMapTime(ageSeconds)
                                               : formatMapTime(0.0),
                                           juce::dontSendNotification);
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

void RealtimeSpecView::mouseExit(const juce::MouseEvent&)
{
    if (! cursorInside)
        return;

    cursorInside = false;
    repaint();
}

juce::Rectangle<float> RealtimeSpecView::getPlotBounds() const noexcept
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
    const auto showHorizontalZoom = freqMode && (horizontalZoom == nullptr
        || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f);
    const auto showVerticalZoom = (freqMode || mapMode) && (verticalZoom == nullptr
        || verticalZoom->load(std::memory_order_relaxed) >= 0.5f);
    if (showHorizontalZoom)
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (showVerticalZoom)
        bounds.removeFromRight(static_cast<float>(bandZoomSliderWidth + ana::ui::gap.pixels()));
    return bounds;
}

void RealtimeSpecView::timerCallback()
{
    if (processor.isOfflineMode())
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
        clearSpectrogram();
    }

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
    const auto mapTimeMilliseconds = processor.getSpecMapTimeMilliseconds();
    const auto mapTimeChanged = ! juce::approximatelyEqual(
        renderedMapTimeMilliseconds, mapTimeMilliseconds);
    const auto frequencyScaleChanged = renderedMapFrequencyScale
        != static_cast<int>(frequencyScale);
    const auto colourMapChanged = renderedMapColourMap != static_cast<int>(colourMap);
    if (viewMode == ViewMode::map
        && (rangeChanged || slopeChanged || highQualityChanged || mapDirectionChanged || mapTimeChanged
            || frequencyScaleChanged || colourMapChanged))
    {
        if (highQualityChanged || mapDirectionChanged || frequencyScaleChanged || mapTimeChanged)
            clearSpectrogram();
        scheduleMapImageRebuild();
        renderedMapHighQuality = highQuality ? 1 : 0;
        renderedMapLeftToRight = mapLeftToRight ? 1 : 0;
        renderedMapFrequencyScale = static_cast<int>(frequencyScale);
        renderedMapColourMap = static_cast<int>(colourMap);
        renderedMapTimeMilliseconds = mapTimeMilliseconds;
    }

    if (viewMode == ViewMode::map && mapImageRebuildPending)
    {
        mapImageRebuildPending = false;
        rebuildRealtimeSpectrogramImage();
        repaint();
    }

    const auto currentRevision = viewMode == ViewMode::map
        ? processor.getSpecProcessor().getMapRevision()
        : processor.getSpecProcessor().getRevision();
    if (displayedRevision != currentRevision)
    {
        displayedRevision = currentRevision;
        if (viewMode == ViewMode::map)
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto rawElapsedMilliseconds = realtimeMapLastAdvanceMilliseconds > 0.0
                ? std::max(0.0, now - realtimeMapLastAdvanceMilliseconds)
                : 1000.0 / 30.0;
            const auto elapsedMilliseconds = rawElapsedMilliseconds <= 250.0
                ? rawElapsedMilliseconds : 1000.0 / 30.0;
            realtimeMapLastAdvanceMilliseconds = now;
            const auto mapWidth = std::max(1, juce::roundToInt(getPlotBounds().getWidth()));
            realtimeMapColumnAccumulator += elapsedMilliseconds * static_cast<double>(mapWidth)
                / std::max(1.0, mapTimeMilliseconds);
            const auto columnCount = static_cast<int>(std::floor(realtimeMapColumnAccumulator));
            if (columnCount > 0)
            {
                realtimeMapColumnAccumulator -= static_cast<double>(columnCount);
                appendSpectrogramFrame(columnCount);
            }
        }
        repaint();
    }
}

void RealtimeSpecView::refreshControls()
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
    const auto splitAvailable = ana::spec::supportsSplitView(monitorMode);
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor);
        monitorButtons[index]->setToggleState(static_cast<int>(index) == modeIndex, juce::dontSendNotification);
    }
    splitButton.setVisible(showMonitor && freqMode);
    splitButton.setEnabled(splitAvailable);
    splitButton.setToggleState(splitAvailable
                                   && readValue(PluginProcessor::specSplitViewParameterId, 0.0f) >= 0.5f,
                               juce::dontSendNotification);
    frequencyRangeSlider.setVisible(showHorizontalZoom && freqMode);
    magnitudeRangeSlider.setVisible(showVerticalZoom && (freqMode || mapMode));

    frequencyLowControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    frequencyHighControl.setVisible((freqMode && showHorizontalReadouts)
        || (mapMode && showVerticalReadouts));
    rangeLowControl.setVisible(freqMode && showVerticalReadouts);
    rangeHighControl.setVisible(freqMode && showVerticalReadouts);
    cursorReadoutLabel.setVisible((freqMode || mapMode) && showCursorHorizontal);
    cursorNoteReadoutLabel.setVisible(showCursorNotes);
    cursorVerticalReadoutLabel.setVisible((freqMode || mapMode) && showCursorVertical);
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void RealtimeSpecView::setViewMode(const ViewMode nextViewMode)
{
    if (viewMode == nextViewMode)
        return;

    viewMode = nextViewMode;
    processor.setSpecMapView(viewMode == ViewMode::map);
    processor.getSpecProcessor().setRealtimeMapMode(viewMode == ViewMode::map);
    cursorInside = false;
    if (viewMode == ViewMode::map)
    {
        resetSpectrogramImage();
    }
    resized();
    repaint();
}

void RealtimeSpecView::scheduleMapImageRebuild() noexcept
{
    mapImageRebuildPending = true;
}

void RealtimeSpecView::resetSpectrogramImage()
{
    const auto bounds = getPlotBounds().toNearestInt();
    const auto width = std::max(1, bounds.getWidth());
    const auto height = std::max(1, bounds.getHeight());

    if (spectrogramImage.isValid()
        && spectrogramImage.getWidth() == width
        && spectrogramImage.getHeight() == height
        && realtimeSpectrogramLevels.size() == static_cast<size_t>(width * height))
        return;

    const auto oldWidth = spectrogramImage.isValid() ? spectrogramImage.getWidth() : 0;
    const auto oldHeight = spectrogramImage.isValid() ? spectrogramImage.getHeight() : 0;
    auto oldLevels = std::move(realtimeSpectrogramLevels);
    realtimeSpectrogramLevels.assign(static_cast<size_t>(width * height), ana::spec::SpecProcessor::minimumDecibels);
    if (oldWidth > 0 && oldHeight > 0
        && oldLevels.size() == static_cast<size_t>(oldWidth * oldHeight))
    {
        for (int y = 0; y < height; ++y)
        {
            const auto sourceY = juce::jlimit(0, oldHeight - 1,
                juce::roundToInt(static_cast<float>(y) * static_cast<float>(oldHeight - 1)
                                 / static_cast<float>(std::max(1, height - 1))));
            for (int x = 0; x < width; ++x)
            {
                const auto sourceX = juce::jlimit(0, oldWidth - 1,
                    juce::roundToInt(static_cast<float>(x) * static_cast<float>(oldWidth - 1)
                                     / static_cast<float>(std::max(1, width - 1))));
                realtimeSpectrogramLevels[static_cast<size_t>(y * width + x)] =
                    oldLevels[static_cast<size_t>(sourceY * oldWidth + sourceX)];
            }
        }
    }

    spectrogramImage = juce::Image(juce::Image::RGB, width, height, true);
    rebuildRealtimeSpectrogramImage();
}

void RealtimeSpecView::rebuildRealtimeSpectrogramImage()
{
    if (! spectrogramImage.isValid()
        || realtimeSpectrogramLevels.size()
            != static_cast<size_t>(spectrogramImage.getWidth() * spectrogramImage.getHeight()))
        return;

    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);

    juce::Image::BitmapData pixels(spectrogramImage, juce::Image::BitmapData::writeOnly);
    const auto width = spectrogramImage.getWidth();
    const auto height = spectrogramImage.getHeight();
    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto normalisedY = localHeight > 1
            ? 1.0f - static_cast<float>(localY) / static_cast<float>(localHeight - 1)
            : 0.0f;
        const auto frequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto rowOffset = static_cast<size_t>(y * width);
        for (int x = 0; x < width; ++x)
        {
            const auto rawValue = realtimeSpectrogramLevels[rowOffset + static_cast<size_t>(x)];
            const auto level = juce::jlimit(0.0f, 1.0f,
                (rawValue + slopeOffset - lowRange) / (highRange - lowRange));
            pixels.setPixelColour(x, y, spectrogramColour(level, colourMap));
        }
    }

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);
}

void RealtimeSpecView::appendSpectrogramFrame(const int columnCount)
{
    resetSpectrogramImage();
    if (! spectrogramImage.isValid())
        return;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);
    const auto useSecondSpectrum = secondChannel != firstChannel;

    std::vector<float> firstSpectrum;
    std::vector<float> secondSpectrum;
    auto fftSize = 0;
    auto secondFftSize = 0;
    const auto sampleRate = processor.getSpecProcessor().getSampleRate();
    processor.getSpecProcessor().copyMapSpectrum(firstChannel, firstSpectrum, fftSize);
    if (useSecondSpectrum)
        processor.getSpecProcessor().copyMapSpectrum(secondChannel, secondSpectrum, secondFftSize);

    if (fftSize <= 0 || sampleRate <= 0.0 || firstSpectrum.empty())
        return;
    if (secondFftSize != fftSize || secondSpectrum.empty())
        secondSpectrum.clear();

    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto colourMap = readSpecColourMap(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto slope = displayRange.slope;
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto width = spectrogramImage.getWidth();
    const auto height = spectrogramImage.getHeight();
    const auto columnsToAppend = juce::jlimit(1, width, columnCount);
    const auto leftToRight = readParameterValue(
        processor, PluginProcessor::specMapLeftToRightParameterId, 0.0f) >= 0.5f;
    const auto directionState = leftToRight ? 1 : 0;
    if (renderedMapLeftToRight != directionState)
    {
        clearSpectrogram();
        renderedMapLeftToRight = directionState;
    }

    auto firstTargetX = width - columnsToAppend;
    auto targetColumnCount = columnsToAppend;
    if (leftToRight)
    {
        if (realtimeMapWriteColumn + columnsToAppend > width)
            clearSpectrogram();
        firstTargetX = juce::jlimit(0, width - 1, realtimeMapWriteColumn);
        targetColumnCount = std::min(columnsToAppend, width - firstTargetX);
    }
    else if (width > columnsToAppend)
    {
        spectrogramImage.moveImageSection(0, 0, columnsToAppend, 0,
                                          width - columnsToAppend, height);
        for (int y = 0; y < height; ++y)
        {
            auto* row = realtimeSpectrogramLevels.data() + static_cast<size_t>(y * width);
            std::move(row + columnsToAppend, row + width, row);
        }
    }

    juce::Image::BitmapData pixels(spectrogramImage, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
    {
        const auto splitHeight = std::max(1, height / 2);
        const auto secondHalf = useSplitView && y >= splitHeight;
        const auto localY = secondHalf ? y - splitHeight : y;
        const auto localHeight = useSplitView
            ? std::max(1, secondHalf ? height - splitHeight : splitHeight)
            : height;
        const auto normalisedY = localHeight > 1
            ? 1.0f - static_cast<float>(localY) / static_cast<float>(localHeight - 1)
            : 0.0f;
        const auto halfPixel = localHeight > 1
            ? 0.5f / static_cast<float>(localHeight - 1) : 0.0f;
        const auto lowerNormalised = juce::jlimit(0.0f, 1.0f, normalisedY - halfPixel);
        const auto upperNormalised = juce::jlimit(0.0f, 1.0f, normalisedY + halfPixel);
        const auto lowerBandFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, lowerNormalised);
        const auto upperBandFrequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, upperNormalised);
        const auto frequency = ana::frequency_scale::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);

        auto rawValue = sampleMapSpectrum(
            secondHalf ? secondSpectrum : firstSpectrum, binFrequency,
            lowerBandFrequency, frequency, upperBandFrequency, highQuality);
        if (! useSplitView && ! secondSpectrum.empty())
            rawValue = std::max(rawValue, sampleMapSpectrum(
                secondSpectrum, binFrequency, lowerBandFrequency, frequency,
                upperBandFrequency, highQuality));

        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto displayValue = rawValue + slopeOffset;
        const auto level = juce::jlimit(0.0f, 1.0f,
                                  (displayValue - lowRange) / (highRange - lowRange));
        const auto colour = spectrogramColour(level, colourMap);
        for (int column = 0; column < targetColumnCount; ++column)
        {
            const auto targetX = firstTargetX + column;
            realtimeSpectrogramLevels[static_cast<size_t>(y * width + targetX)] = rawValue;
            pixels.setPixelColour(targetX, y, colour);
        }
    }

    if (leftToRight)
        realtimeMapWriteColumn += targetColumnCount;

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = highQuality ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    renderedMapColourMap = static_cast<int>(colourMap);
}

void RealtimeSpecView::syncRangeSliders()
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
    updateCursorReadouts();

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, lowFrequency),
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, highFrequency));
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

void RealtimeSpecView::updateFrequencyRangeFromSlider()
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

void RealtimeSpecView::updateVerticalRangeFromSlider()
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

        clearSpectrogram();
        repaint();
        return;
    }

    constexpr auto span = ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplayDecibels;
    const auto highRange = ana::spec::maximumDisplayDecibels - magnitudeRangeSlider.getRangeStart() * span;
    const auto lowRange = ana::spec::maximumDisplayDecibels - magnitudeRangeSlider.getRangeEnd() * span;
    rangeLowControl.getSlider().setValue(lowRange, juce::sendNotificationSync);
    rangeHighControl.getSlider().setValue(highRange, juce::sendNotificationSync);

    // Recolour on the refresh cadence, not every mouseDrag, to keep large maps responsive.
    if (viewMode == ViewMode::map)
        scheduleMapImageRebuild();
    repaint();
}
