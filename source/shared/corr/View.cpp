#include "View.h"
#include "shared/spec/CursorNote.h"
#include "shared/shell/CleanView.h"
#include "shared/corr/Settings.h"
#include "shared/spec/SpectrumProcessing.h"
#include "shared/spec/Smoothing.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/GraphColours.h"

#include <algorithm>
#include <cmath>

using namespace ana::ui::analyzer_detail;

namespace
{
ana::frequency_scale::Scale readCorrFrequencyScale(const PluginProcessor& processor) noexcept
{
    return ana::frequency_scale::scaleFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::corrFrequencyScaleParameterId,
        static_cast<float>(ana::frequency_scale::defaultScaleIndex))));
}

juce::Colour readGraphColour(const PluginProcessor& processor, const char* parameterId,
                             const int defaultIndex) noexcept
{
    return ana::ui::graphColour(juce::roundToInt(readParameterValue(
        processor, parameterId, static_cast<float>(defaultIndex))));
}

float readGraphOpacity(const PluginProcessor& processor) noexcept
{
    return juce::jlimit(0.01f, 1.0f, readParameterValue(
        processor, PluginProcessor::corrGraphOpacityParameterId, 100.0f) * 0.01f);
}
}

CorrView::CorrView(PluginProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), PluginProcessor::corrLowParameterId,
                          "LOW", [] (const double value) { return formatReadoutFrequency(value); }),
      frequencyHighControl(processorRef.getParameters(), PluginProcessor::corrHighParameterId,
                           "HIGH", [] (const double value) { return formatReadoutFrequency(value); }),
      rangeLowControl(processorRef.getParameters(), PluginProcessor::corrRangeLowParameterId,
                      "LOW", [] (const double value) { return formatCorrCoefficient(value); }),
      rangeHighControl(processorRef.getParameters(), PluginProcessor::corrRangeHighParameterId,
                       "HIGH", [] (const double value) { return formatCorrCoefficient(value); })
{
    for (auto* component : std::array<juce::Component*, 11> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &cursorReadoutLabel, &cursorNoteReadoutLabel, &cursorVerticalReadoutLabel, &phaseModeButton, &freqModeButton,
             &signedModeButton, &frequencyRangeSlider })
        addAndMakeVisible(*component);
    addAndMakeVisible(corrRangeSlider);

    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
    {
        control->setCompact(true);
        control->onValueChanged = [this] { repaint(); };
    }
    configureCursorReadoutLabel(cursorReadoutLabel);
    configureCursorReadoutLabel(cursorNoteReadoutLabel);
    cursorNoteReadoutLabel.setBorderSize(juce::BorderSize<int>(1, 4, 1, 4));

    configureCursorReadoutLabel(cursorVerticalReadoutLabel);

    phaseModeButton.onClick = [this]
    {
        setCorrMode(ana::corr::CorrProcessor::modeIndex(
            ana::corr::CorrProcessor::Mode::phase));
    };
    freqModeButton.onClick = [this]
    {
        setCorrMode(ana::corr::CorrProcessor::modeIndex(
            ana::corr::CorrProcessor::Mode::frequency));
    };
    signedModeButton.onClick = [this]
    {
        setCorrMode(ana::corr::CorrProcessor::modeIndex(
            ana::corr::CorrProcessor::Mode::signedCorrelation));
    };
    frequencyRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateFrequencyRangeFromSlider();
    };
    corrRangeSlider.onRangeChanged = [this]
    {
        if (! synchronisingRanges)
            updateCorrRangeFromSlider();
    };
    startTimerHz(30);
}

void CorrView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::background);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    const auto modeIndex = getCorrModeIndex();
    const auto mode = ana::corr::CorrProcessor::modeFromIndex(modeIndex);
    const auto displayType = [this] (const char* parameterId)
    {
        const auto* value = processor.getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::corr::CorrProcessor::DisplayType::minimum
            : ana::corr::CorrProcessor::DisplayType::average;
    };
    // Retain the result for the whole paint: the worker may publish a replacement.
    const auto offline = processor.isOfflineMode();
    const auto analysis = offline ? processor.getAnalysisResult() : nullptr;
    if (offline && (analysis == nullptr || analysis->corr == nullptr))
        return;
    const auto* displayedCorr = offline ? analysis->corr.get() : &processor.getCorrProcessor();
    const auto sampleRate = offline ? analysis->corrSampleRate : displayedCorr->getSampleRate();

    const auto primaryDisplayType = displayType(PluginProcessor::corrFirstGraphTypeParameterId);
    const auto primaryColour = readGraphColour(processor,
        PluginProcessor::corrFirstGraphColourParameterId, ana::ui::defaultFirstGraphColourIndex);
    const auto secondaryColour = readGraphColour(processor,
        PluginProcessor::corrSecondGraphColourParameterId, ana::ui::defaultSecondGraphColourIndex);
    const auto graphOpacity = readGraphOpacity(processor);
    displayedCorr->copyCorr(mode, primaryDisplayType, primaryCorr, fftSize);
    if (fftSize <= 0 || primaryCorr.empty())
        return;

    const auto lowFrequency = std::max(ana::frequency_scale::minimumHz, readParameterValue(processor, PluginProcessor::corrLowParameterId, ana::frequency_scale::minimumHz));
    const auto highFrequency = std::max(lowFrequency + ana::frequency_scale::minimumSpanHz,
                                        readParameterValue(processor, PluginProcessor::corrHighParameterId, ana::frequency_scale::maximumHz));
    const auto lowRange = readParameterValue(processor, PluginProcessor::corrRangeLowParameterId,
                                        modeIndex == 1 ? ana::corr::frequencyModeMinimumCoefficient
                                                       : ana::corr::defaultLowCoefficient);
    const auto highRange = std::max(lowRange + ana::corr::minimumCoefficientSpan,
                                    readParameterValue(processor, PluginProcessor::corrRangeHighParameterId,
                                                       ana::corr::defaultHighCoefficient));
    const auto zeroY = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
        (0.0f - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
    drawCorr(graphics, primaryCorr, plotBounds, lowFrequency, highFrequency,
                    lowRange, highRange, sampleRate, fftSize,
                    primaryColour,
                    primaryColour.withAlpha(graphOpacity));

    const auto* secondGraph = processor.getRawParameterValue(
        PluginProcessor::corrSecondGraphParameterId);
    if (secondGraph != nullptr && secondGraph->load(std::memory_order_relaxed) >= 0.5f)
    {
        int secondaryFftSize = 0;
        const auto secondaryDisplayType = displayType(PluginProcessor::corrSecondGraphTypeParameterId);
        displayedCorr->copyCorr(
            mode, secondaryDisplayType, secondaryCorr, secondaryFftSize);
        if (secondaryFftSize == fftSize)
        {
            drawCorr(graphics, secondaryCorr, plotBounds, lowFrequency, highFrequency,
                            lowRange, highRange, sampleRate, fftSize,
                            secondaryColour,
                            secondaryColour.withAlpha(graphOpacity));
        }
    }

    graphics.setColour(ana::ui::light);
    graphics.fillRect(plotBounds.getX(), zeroY, plotBounds.getWidth(), 1.0f);

    const auto* cursorReadout = processor.getRawParameterValue(
        PluginProcessor::corrCursorReadoutParameterId);
    const auto* cursorVerticalReadout = processor.getRawParameterValue(
        PluginProcessor::corrCursorVerticalReadoutParameterId);
    if (! processor.isCleanView() && cursorInside)
    {
        graphics.setColour(ana::ui::white);
        const auto* cursorNotes = processor.getRawParameterValue(PluginProcessor::corrCursorNotesParameterId);
        if (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f
            || cursorNotes == nullptr || cursorNotes->load(std::memory_order_relaxed) >= 0.5f)
            graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        if (cursorVerticalReadout == nullptr || cursorVerticalReadout->load(std::memory_order_relaxed) >= 0.5f)
            graphics.drawLine(plotBounds.getX(), cursorPosition.y, plotBounds.getRight(), cursorPosition.y, 0.5f);
    }
}

void CorrView::drawCorr(juce::Graphics& graphics,
                                                      const std::vector<float>& values,
                                                      const juce::Rectangle<float> plotBounds,
                                                      const float lowFrequency,
                                                      const float highFrequency,
                                                      const float lowRange,
                                                      const float highRange,
                                                      const double sampleRate,
                                                      const int fftSize,
                                                      const juce::Colour lineColour,
                                                      const juce::Colour fillColour)
{
    if (fftSize <= 0 || sampleRate <= 0.0 || values.empty() || plotBounds.isEmpty())
        return;

    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto frequencyScale = readCorrFrequencyScale(processor);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    const auto* smoothingParameter = processor.getRawParameterValue(
        PluginProcessor::corrSmoothingParameterId);
    const auto smoothing = smoothingParameter != nullptr
        ? smoothingParameter->load(std::memory_order_relaxed)
        : ana::spectrum_processing::defaultSmoothingPercent;
    const ana::spectrum_processing::FrequencySmoothing curve(
        std::vector<double>(values.begin(), values.end()), smoothing);
    const auto displayColumns = curve.displayColumns(frequencyScale, lowFrequency, highFrequency,
                                                     binFrequency, columnCount);

    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    auto hasPoint = false;
    for (int column = 0; column < columnCount; ++column)
    {
        const auto value = static_cast<float>(displayColumns[static_cast<size_t>(column)]);
        if (! std::isfinite(value))
            continue;

        const auto y = plotBounds.getBottom() - juce::jlimit(0.0f, 1.0f,
            (value - lowRange) / (highRange - lowRange)) * plotBounds.getHeight();
        const auto point = juce::Point<float>(plotBounds.getX() + static_cast<float>(column) + 0.5f, y);
        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
        }
        lastPoint = point;
    }

    if (! hasPoint)
        return;

    const auto* filled = processor.getRawParameterValue(
        PluginProcessor::corrFilledDisplayParameterId);
    if (filled == nullptr || filled->load(std::memory_order_relaxed) >= 0.5f)
    {
        auto fillPath = path;
        fillPath.lineTo(lastPoint.x, plotBounds.getBottom());
        fillPath.lineTo(firstPoint.x, plotBounds.getBottom());
        fillPath.closeSubPath();
        graphics.setColour(fillColour);
        graphics.fillPath(fillPath);
    }

    graphics.setColour(lineColour);
    graphics.strokePath(path, juce::PathStrokeType(1.0f));
}

void CorrView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto* cursor = processor.getRawParameterValue(
        PluginProcessor::corrCursorReadoutParameterId);
    const auto* verticalCursor = processor.getRawParameterValue(
        PluginProcessor::corrCursorVerticalReadoutParameterId);
    const auto* notes = processor.getRawParameterValue(PluginProcessor::corrCursorNotesParameterId);
    const auto showCursorNotes = notes == nullptr || notes->load(std::memory_order_relaxed) >= 0.5f;
    const auto showCursorHorizontal = cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f;
    const auto showCursorVertical = verticalCursor == nullptr || verticalCursor->load(std::memory_order_relaxed) >= 0.5f;
    const auto* horizontalZoom = processor.getRawParameterValue(
        PluginProcessor::corrHorizontalZoomParameterId);
    const auto* verticalZoom = processor.getRawParameterValue(
        PluginProcessor::corrVerticalZoomParameterId);
    const auto showHorizontalZoom = horizontalZoom == nullptr
        || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f;
    const auto showVerticalZoom = verticalZoom == nullptr
        || verticalZoom->load(std::memory_order_relaxed) >= 0.5f;
    const auto frequencyReadoutWidth = ana::ui::textControlWidth(8);
    const auto coefficientReadoutWidth = ana::ui::textControlWidth(5);
    const auto readoutY = plotBounds.getBottom() - ana::ui::controlHeight;
    const auto graphRight = plotBounds.getRight();
    auto topArea = juce::Rectangle<int>(plotBounds.getX(), plotBounds.getY(),
                                        plotBounds.getWidth(), ana::ui::controlHeight);
    auto topReadouts = topArea.removeFromRight(coefficientReadoutWidth);
    ana::ui::gap.removeFromRight(topArea);
    ana::ui::FixedGapRow topControls(topArea);
    signedModeButton.setBounds(topControls.takeLeft(signedModeButton.getPreferredWidth()));
    phaseModeButton.setBounds(topControls.takeLeft(phaseModeButton.getPreferredWidth()));
    freqModeButton.setBounds(topControls.takeLeft(freqModeButton.getPreferredWidth()));
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    rangeHighControl.setBounds(topReadoutControls.takeLeft(coefficientReadoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, frequencyReadoutWidth, ana::ui::controlHeight);
    const auto* verticalReadouts = processor.getRawParameterValue(
        PluginProcessor::corrVerticalReadoutsParameterId);
    const auto showVerticalReadouts = verticalReadouts == nullptr
        || verticalReadouts->load(std::memory_order_relaxed) >= 0.5f;
    frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth
                                       - (showVerticalReadouts ? coefficientReadoutWidth + ana::ui::gap.pixels() : 0), readoutY,
                                   frequencyReadoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - coefficientReadoutWidth, readoutY,
                              coefficientReadoutWidth, ana::ui::controlHeight);
    cursorReadoutLabel.setBounds({});
    cursorNoteReadoutLabel.setBounds({});
    cursorVerticalReadoutLabel.setBounds({});
    if (showCursorHorizontal || showCursorNotes || showCursorVertical)
    {
        const auto left = frequencyLowControl.getRight() + ana::ui::gap.pixels();
        const auto right = frequencyHighControl.getX() - ana::ui::gap.pixels();
        const auto noteWidth = ana::ui::textControlWidth(4) - 2 * (ana::ui::textPadding - 4);
        const auto baseWidth = (showCursorHorizontal ? frequencyReadoutWidth : 0)
            + (showCursorNotes ? (showCursorHorizontal ? ana::ui::gap.pixels() : 0) + noteWidth : 0);
        const auto verticalFits = showCursorVertical && baseWidth + (baseWidth > 0 ? ana::ui::gap.pixels() : 0)
            + coefficientReadoutWidth <= right - left;
        const auto cursorWidth = baseWidth
            + (verticalFits ? (baseWidth > 0 ? ana::ui::gap.pixels() : 0) + coefficientReadoutWidth : 0);
        const auto cursorX = juce::jlimit(left,
            std::max(left, right - cursorWidth),
            plotBounds.getCentreX() - cursorWidth / 2);
        if (showCursorHorizontal)
            cursorReadoutLabel.setBounds(cursorX, readoutY,
                                         frequencyReadoutWidth, ana::ui::controlHeight);
        if (showCursorNotes)
            cursorNoteReadoutLabel.setBounds(cursorX
                + (showCursorHorizontal ? frequencyReadoutWidth + ana::ui::gap.pixels() : 0),
                readoutY, noteWidth, ana::ui::controlHeight);
        if (showCursorVertical)
            cursorVerticalReadoutLabel.setBounds(verticalFits
                                                     ? cursorX + baseWidth + (baseWidth > 0 ? ana::ui::gap.pixels() : 0)
                                                     : graphRight - coefficientReadoutWidth,
                                                 verticalFits ? readoutY
                                                     : readoutY - ana::ui::controlHeight - ana::ui::gap.pixels(),
                                                 coefficientReadoutWidth, ana::ui::controlHeight);
    }
    frequencyRangeSlider.setBounds(showHorizontalZoom
        ? juce::Rectangle<int>(0, getHeight() - bandRangeSliderHeight,
                               getWidth() - (showVerticalZoom
                                   ? bandRangeSliderHeight + ana::ui::gap.pixels() : 0),
                               bandRangeSliderHeight)
        : juce::Rectangle<int>());
    corrRangeSlider.setBounds(showVerticalZoom
        ? juce::Rectangle<int>(getWidth() - bandRangeSliderHeight, 0,
                               bandRangeSliderHeight, getHeight())
        : juce::Rectangle<int>());
    syncRangeSliders();
    refreshControls();
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void CorrView::mouseDown(const juce::MouseEvent& event)
{
    const auto* resetClick = processor.getRawParameterValue(
        PluginProcessor::corrResetClickParameterId);
    if (! processor.isOfflineMode() && event.originalComponent == this
        && ! processor.getCorrProcessor().isFrozen()
        && resetClick != nullptr && resetClick->load(std::memory_order_relaxed) >= 0.5f)
        processor.clearCorrProcessor();
}

void CorrView::mouseMove(const juce::MouseEvent& event)
{
    if (processor.isCleanView())
    {
        cursorInside = false;
        return;
    }

    const auto plotBounds = getPlotBounds();
    const auto nextCursorInside = plotBounds.contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;
    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto* low = processor.getRawParameterValue(PluginProcessor::corrLowParameterId);
        const auto* high = processor.getRawParameterValue(PluginProcessor::corrHighParameterId);
        const auto lowFrequency = std::max(ana::frequency_scale::minimumHz, low != nullptr ? low->load(std::memory_order_relaxed) : ana::frequency_scale::minimumHz);
        const auto highFrequency = std::max(lowFrequency + ana::frequency_scale::minimumSpanHz,
            high != nullptr ? high->load(std::memory_order_relaxed) : ana::frequency_scale::maximumHz);
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
        lastCursorFrequency = ana::frequency_scale::frequencyAt(
            readCorrFrequencyScale(processor), lowFrequency, highFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        cursorNoteReadoutLabel.setText(ana::ui::cursorNoteName(lastCursorFrequency), juce::dontSendNotification);
        const auto* lowRangeParameter = processor.getRawParameterValue(
            PluginProcessor::corrRangeLowParameterId);
        const auto* highRangeParameter = processor.getRawParameterValue(
            PluginProcessor::corrRangeHighParameterId);
        const auto lowRange = lowRangeParameter != nullptr
            ? lowRangeParameter->load(std::memory_order_relaxed)
            : (getCorrModeIndex()
                   == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency)
               ? ana::corr::frequencyModeMinimumCoefficient
               : ana::corr::defaultLowCoefficient);
        const auto highRange = std::max(lowRange + ana::corr::minimumCoefficientSpan,
            highRangeParameter != nullptr ? highRangeParameter->load(std::memory_order_relaxed)
                                          : ana::corr::defaultHighCoefficient);
        const auto normalisedY = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.y - plotBounds.getY()) / plotBounds.getHeight());
        cursorVerticalReadoutLabel.setText(formatCorrCoefficient(
                                               highRange - normalisedY * (highRange - lowRange)),
                                           juce::dontSendNotification);
    }
    repaint();
}

void CorrView::mouseExit(const juce::MouseEvent&)
{
    cursorInside = false;
    repaint();
}

void CorrView::refreshMode()
{
    displayedOffline = processor.isOfflineMode();
    displayedRevision = 0;
    cursorInside = false;
    primaryCorr.clear();
    secondaryCorr.clear();
    refreshControls();
    resized();
    repaint();
}

void CorrView::timerCallback()
{
    if (displayedOffline != processor.isOfflineMode())
        refreshMode();
    syncRangeSliders();
    refreshControls();
    if (processor.getAnalyzerPageState() != ana::AnalyzerPage::corr)
        return;

    auto revision = processor.getCorrProcessor().getRevision();
    if (displayedOffline)
    {
        processor.requestOfflineAnalysis(size_t { 1 });
        const auto analysis = processor.getAnalysisResult();
        revision = analysis != nullptr ? analysis->revision : 0;
    }
    if (displayedRevision != revision)
    {
        displayedRevision = revision;
        repaint();
    }
}

juce::String CorrView::getSettingsViewModeName() const
{
    switch (ana::corr::CorrProcessor::modeFromIndex(getCorrModeIndex()))
    {
        case ana::corr::CorrProcessor::Mode::frequency: return "FREQ";
        case ana::corr::CorrProcessor::Mode::signedCorrelation: return "SIGNED";
        case ana::corr::CorrProcessor::Mode::phase:
        default: return "PHASE";
    }
}

void CorrView::setCorrMode(const int mode)
{
    processor.setCorrMode(mode);
    resized();
    repaint();

}

int CorrView::getCorrModeIndex() const noexcept
{
    if (const auto* mode = processor.getRawParameterValue(
            PluginProcessor::corrModeParameterId))
        return juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                             juce::roundToInt(mode->load(std::memory_order_relaxed)));

    return ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::phase);
}

void CorrView::syncRangeSliders()
{
    const auto read = [this] (const char* id, const float fallback)
    {
        if (const auto* value = processor.getRawParameterValue(id))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = read(PluginProcessor::corrLowParameterId, ana::frequency_scale::minimumHz);
    const auto highFrequency = read(PluginProcessor::corrHighParameterId, ana::frequency_scale::maximumHz);
    const auto frequencyMode = getCorrModeIndex()
        == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency);
    const auto minimumRange = ana::corr::rangeMinimum(frequencyMode);
    const auto lowRange = std::max(minimumRange,
                                   read(PluginProcessor::corrRangeLowParameterId, minimumRange));
    const auto highRange = read(PluginProcessor::corrRangeHighParameterId,
                                ana::corr::defaultHighCoefficient);
    const auto* horizontalReadouts = processor.getRawParameterValue(
        PluginProcessor::corrHorizontalReadoutsParameterId);
    const auto* verticalReadouts = processor.getRawParameterValue(
        PluginProcessor::corrVerticalReadoutsParameterId);
    const auto* cursor = processor.getRawParameterValue(PluginProcessor::corrCursorReadoutParameterId);
    const auto* verticalCursor = processor.getRawParameterValue(
        PluginProcessor::corrCursorVerticalReadoutParameterId);
    const auto showHorizontalReadouts = horizontalReadouts == nullptr
        || horizontalReadouts->load(std::memory_order_relaxed) >= 0.5f;
    const auto showVerticalReadouts = verticalReadouts == nullptr
        || verticalReadouts->load(std::memory_order_relaxed) >= 0.5f;
    const auto* notes = processor.getRawParameterValue(PluginProcessor::corrCursorNotesParameterId);
    cursorNoteReadoutLabel.setVisible(notes == nullptr || notes->load(std::memory_order_relaxed) >= 0.5f);
    frequencyLowControl.setVisible(showHorizontalReadouts);
    frequencyHighControl.setVisible(showHorizontalReadouts);
    rangeLowControl.setVisible(showVerticalReadouts);
    rangeHighControl.setVisible(showVerticalReadouts);
    cursorReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(verticalCursor == nullptr
        || verticalCursor->load(std::memory_order_relaxed) >= 0.5f);
    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    const auto frequencyScale = readCorrFrequencyScale(processor);
    frequencyRangeSlider.setRange(
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, std::min(lowFrequency, highFrequency)),
        ana::frequency_scale::normalisedForFrequency(
            frequencyScale, ana::frequency_scale::minimumHz,
            ana::frequency_scale::maximumHz, std::max(lowFrequency, highFrequency)));
    corrRangeSlider.setRange(
        ana::corr::toInvertedNormalised(std::max(lowRange, highRange), frequencyMode),
        ana::corr::toInvertedNormalised(std::min(lowRange, highRange), frequencyMode));
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void CorrView::refreshControls()
{
    const auto* horizontalZoom = processor.getRawParameterValue(
        PluginProcessor::corrHorizontalZoomParameterId);
    const auto* verticalZoom = processor.getRawParameterValue(
        PluginProcessor::corrVerticalZoomParameterId);
    const auto mode = getCorrModeIndex();
    const auto freqMode = mode
        == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency);
    phaseModeButton.setVisible(true);
    freqModeButton.setVisible(true);
    signedModeButton.setVisible(true);
    phaseModeButton.setToggleState(
        mode == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::phase),
        juce::dontSendNotification);
    freqModeButton.setToggleState(
        mode == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency),
        juce::dontSendNotification);
    signedModeButton.setToggleState(
        mode == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::signedCorrelation),
        juce::dontSendNotification);
    auto& lowRangeSlider = rangeLowControl.getSlider();
    auto& highRangeSlider = rangeHighControl.getSlider();
    const auto minimumRange = static_cast<double>(ana::corr::rangeMinimum(freqMode));
    lowRangeSlider.setRange(minimumRange, ana::corr::maximumCoefficient, ana::corr::coefficientStep);
    highRangeSlider.setRange(minimumRange, ana::corr::maximumCoefficient, ana::corr::coefficientStep);

    // setRange() may clamp only the Slider's local value; restore from the active APVTS parameter.
    const auto readRangeParameter = [this] (const char* parameterId, const double fallback)
    {
        if (const auto* value = processor.getRawParameterValue(parameterId))
            return static_cast<double>(value->load(std::memory_order_relaxed));
        return fallback;
    };
    lowRangeSlider.setValue(juce::jlimit(minimumRange, static_cast<double>(ana::corr::maximumCoefficient),
                                         readRangeParameter(PluginProcessor::corrRangeLowParameterId,
                                                            minimumRange)),
                            juce::dontSendNotification);
    highRangeSlider.setValue(juce::jlimit(minimumRange, static_cast<double>(ana::corr::maximumCoefficient),
                                          readRangeParameter(PluginProcessor::corrRangeHighParameterId,
                                                             ana::corr::defaultHighCoefficient)),
                             juce::dontSendNotification);

    frequencyRangeSlider.setVisible(horizontalZoom == nullptr
        || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f);
    corrRangeSlider.setVisible(verticalZoom == nullptr
        || verticalZoom->load(std::memory_order_relaxed) >= 0.5f);
    if (processor.isCleanView())
        ana::ui::hidePlotControls(*this);
}

void CorrView::updateFrequencyRangeFromSlider()
{
    const auto frequencyScale = readCorrFrequencyScale(processor);
    frequencyLowControl.getSlider().setValue(ana::frequency_scale::frequencyAt(
                                                  frequencyScale,
                                                  ana::frequency_scale::minimumHz,
                                                  ana::frequency_scale::maximumHz,
                                                  frequencyRangeSlider.getRangeStart()),
                                              juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(ana::frequency_scale::frequencyAt(
                                                   frequencyScale,
                                                   ana::frequency_scale::minimumHz,
                                                   ana::frequency_scale::maximumHz,
                                                   frequencyRangeSlider.getRangeEnd()),
                                               juce::sendNotificationSync);
}

void CorrView::updateCorrRangeFromSlider()
{
    const auto frequencyMode = getCorrModeIndex()
        == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency);
    rangeHighControl.getSlider().setValue(
        ana::corr::fromInvertedNormalised(
            static_cast<float>(corrRangeSlider.getRangeStart()), frequencyMode),
        juce::sendNotificationSync);
    rangeLowControl.getSlider().setValue(
        ana::corr::fromInvertedNormalised(
            static_cast<float>(corrRangeSlider.getRangeEnd()), frequencyMode),
        juce::sendNotificationSync);
}

juce::Rectangle<float> CorrView::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    if (processor.isCleanView())
        return bounds;
    const auto* horizontalZoom = processor.getRawParameterValue(
        PluginProcessor::corrHorizontalZoomParameterId);
    const auto* verticalZoom = processor.getRawParameterValue(
        PluginProcessor::corrVerticalZoomParameterId);
    if (horizontalZoom == nullptr || horizontalZoom->load(std::memory_order_relaxed) >= 0.5f)
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (verticalZoom == nullptr || verticalZoom->load(std::memory_order_relaxed) >= 0.5f)
        bounds.removeFromRight(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    return bounds;
}
