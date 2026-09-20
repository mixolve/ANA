#include "View.h"
#include "shared/corr/Settings.h"
#include "shared/analyzer/DisplaySettings.h"
#include "AnalyzerViewUtilities.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace ana::ui::analyzer_detail;

namespace
{
ana::analyzer_frequency::Scale readCorrFrequencyScale(const PluginProcessor& processor) noexcept
{
    return ana::analyzer_frequency::scaleFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::corrFrequencyScaleParameterId,
        static_cast<float>(ana::analyzer_frequency::defaultScaleIndex))));
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
    for (auto* component : std::array<juce::Component*, 10> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &cursorReadoutLabel, &cursorVerticalReadoutLabel, &phaseModeButton, &freqModeButton,
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
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    int fftSize = 0;
    const auto modeIndex = getCorrModeIndex();
    const auto mode = ana::corr::CorrProcessor::modeFromIndex(modeIndex);
    const auto displayType = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f
            ? ana::corr::CorrProcessor::DisplayType::minimum
            : ana::corr::CorrProcessor::DisplayType::average;
    };
    const auto* displayedCorr = &processor.getCorrProcessor();
    auto sampleRate = displayedCorr->getSampleRate();

    const auto primaryDisplayType = displayType(PluginProcessor::corrFirstGraphTypeParameterId);
    displayedCorr->copyCorr(mode, primaryDisplayType, primaryCorr, fftSize);
    if (fftSize <= 0 || primaryCorr.empty())
        return;

    const auto lowFrequency = std::max(ana::analyzer_frequency::minimumHz, readParameterValue(processor, PluginProcessor::corrLowParameterId, ana::analyzer_frequency::minimumHz));
    const auto highFrequency = std::max(lowFrequency + ana::analyzer_frequency::minimumSpanHz,
                                        readParameterValue(processor, PluginProcessor::corrHighParameterId, ana::analyzer_frequency::maximumHz));
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
                    ana::ui::white, ana::ui::light);

    const auto* secondGraph = processor.getParameters().getRawParameterValue(
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
                            ana::ui::white, ana::ui::dark);
        }
    }

    graphics.setColour(ana::ui::light);
    graphics.fillRect(plotBounds.getX(), zeroY, plotBounds.getWidth(), 1.0f);

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
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
    if (fftSize <= 0 || values.empty())
        return;

    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto frequencyScale = readCorrFrequencyScale(processor);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));
    const auto* smoothingParameter = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrSmoothingParameterId);
    const auto smoothing = smoothingParameter != nullptr
        ? smoothingParameter->load(std::memory_order_relaxed)
        : ana::analyzer_display::defaultSmoothingPercent;
    const auto smoothingRadius = ana::analyzer_display::smoothingRadius(smoothing);
    const auto smoothingSigma = ana::analyzer_display::smoothingSigma(smoothingRadius);

    const auto makeDisplayColumns = [&] (const std::vector<float>& source)
    {
        std::vector<float> sums(static_cast<size_t>(columnCount), 0.0f);
        std::vector<int> counts(static_cast<size_t>(columnCount), 0);
        for (size_t bin = 1; bin < source.size(); ++bin)
        {
            const auto frequency = static_cast<float>(bin) * binFrequency;
            if (frequency < lowFrequency || frequency > highFrequency)
                continue;
            const auto normalisedFrequency = ana::analyzer_frequency::normalisedForFrequency(
                frequencyScale, lowFrequency, highFrequency, frequency);
            const auto column = juce::jlimit(0, columnCount - 1,
                static_cast<int>(std::floor(normalisedFrequency * plotBounds.getWidth())));
            sums[static_cast<size_t>(column)] += source[bin];
            ++counts[static_cast<size_t>(column)];
        }

        std::vector<float> display(static_cast<size_t>(columnCount),
                                   std::numeric_limits<float>::quiet_NaN());
        for (int column = 0; column < columnCount; ++column)
        {
            auto summedCorr = 0.0f;
            auto summedWeight = 0.0f;
            const auto firstColumn = std::max(0, column - smoothingRadius);
            const auto lastColumn = std::min(columnCount - 1, column + smoothingRadius);
            for (int neighbour = firstColumn; neighbour <= lastColumn; ++neighbour)
            {
                const auto count = counts[static_cast<size_t>(neighbour)];
                if (count == 0)
                    continue;

                const auto distance = static_cast<float>(std::abs(neighbour - column));
                const auto weight = std::exp(-0.5f * distance * distance
                                             / (smoothingSigma * smoothingSigma));
                summedCorr += sums[static_cast<size_t>(neighbour)] * weight;
                summedWeight += static_cast<float>(count) * weight;
            }
            if (summedWeight > 0.0f)
                display[static_cast<size_t>(column)] = summedCorr / summedWeight;
        }
        return display;
    };

    auto displayColumns = makeDisplayColumns(values);

    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    auto hasPoint = false;
    for (int column = 0; column < columnCount; ++column)
    {
        const auto value = displayColumns[static_cast<size_t>(column)];
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

    const auto* filled = processor.getParameters().getRawParameterValue(
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
    const auto* cursor = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrCursorReadoutParameterId);
    const auto showCursor = cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f;
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    constexpr int frequencyReadoutWidth = ana::ui::textControlWidth(8);
    constexpr int coefficientReadoutWidth = ana::ui::textControlWidth(5);
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
    cursorReadoutLabel.setBounds(showCursor
        ? topControls.takeLeft(frequencyReadoutWidth) : juce::Rectangle<int>());
    const auto cursorVerticalReadoutFits = showCursor
        && topControls.remaining().getWidth() >= coefficientReadoutWidth;
    cursorVerticalReadoutLabel.setBounds(cursorVerticalReadoutFits
        ? topControls.takeLeft(coefficientReadoutWidth) : juce::Rectangle<int>());
    ana::ui::FixedGapRow topReadoutControls(topReadouts);
    rangeHighControl.setBounds(topReadoutControls.takeLeft(coefficientReadoutWidth));
    frequencyLowControl.setBounds(plotBounds.getX(), readoutY, frequencyReadoutWidth, ana::ui::controlHeight);
    frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth - coefficientReadoutWidth
                                       - ana::ui::gap.pixels(), readoutY,
                                   frequencyReadoutWidth, ana::ui::controlHeight);
    rangeLowControl.setBounds(graphRight - coefficientReadoutWidth, readoutY,
                              coefficientReadoutWidth, ana::ui::controlHeight);
    if (showZoom)
    {
        frequencyRangeSlider.setBounds(0, getHeight() - bandRangeSliderHeight,
                                       getWidth(), bandRangeSliderHeight);
        corrRangeSlider.setBounds(getWidth() - bandRangeSliderHeight, 0, bandRangeSliderHeight,
                                         getHeight() - bandRangeSliderHeight - ana::ui::gap.pixels());
    }
    else
    {
        frequencyRangeSlider.setBounds({});
        corrRangeSlider.setBounds({});
    }
    syncRangeSliders();
    refreshControls();
}

void CorrView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent == this && ! processor.getCorrProcessor().isFrozen())
        processor.clearCorrProcessor();
}

void CorrView::mouseMove(const juce::MouseEvent& event)
{
    const auto plotBounds = getPlotBounds();
    const auto nextCursorInside = plotBounds.contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;
    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    if (cursorInside)
    {
        const auto* low = processor.getParameters().getRawParameterValue(PluginProcessor::corrLowParameterId);
        const auto* high = processor.getParameters().getRawParameterValue(PluginProcessor::corrHighParameterId);
        const auto lowFrequency = std::max(ana::analyzer_frequency::minimumHz, low != nullptr ? low->load(std::memory_order_relaxed) : ana::analyzer_frequency::minimumHz);
        const auto highFrequency = std::max(lowFrequency + ana::analyzer_frequency::minimumSpanHz,
            high != nullptr ? high->load(std::memory_order_relaxed) : ana::analyzer_frequency::maximumHz);
        const auto normalisedX = juce::jlimit(0.0f, 1.0f,
            (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
        lastCursorFrequency = ana::analyzer_frequency::frequencyAt(
            readCorrFrequencyScale(processor), lowFrequency, highFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        const auto* lowRangeParameter = processor.getParameters().getRawParameterValue(
            PluginProcessor::corrRangeLowParameterId);
        const auto* highRangeParameter = processor.getParameters().getRawParameterValue(
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

void CorrView::timerCallback()
{
    syncRangeSliders();
    refreshControls();
    const auto revision = processor.getCorrProcessor().getRevision();
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
    if (const auto* mode = processor.getParameters().getRawParameterValue(
            PluginProcessor::corrModeParameterId))
        return juce::jlimit(0, ana::corr::CorrProcessor::modeCount - 1,
                             juce::roundToInt(mode->load(std::memory_order_relaxed)));

    return ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::phase);
}

void CorrView::syncRangeSliders()
{
    const auto read = [this] (const char* id, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(id))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto lowFrequency = read(PluginProcessor::corrLowParameterId, ana::analyzer_frequency::minimumHz);
    const auto highFrequency = read(PluginProcessor::corrHighParameterId, ana::analyzer_frequency::maximumHz);
    const auto frequencyMode = getCorrModeIndex()
        == ana::corr::CorrProcessor::modeIndex(ana::corr::CorrProcessor::Mode::frequency);
    const auto minimumRange = ana::corr::rangeMinimum(frequencyMode);
    const auto lowRange = std::max(minimumRange,
                                   read(PluginProcessor::corrRangeLowParameterId, minimumRange));
    const auto highRange = read(PluginProcessor::corrRangeHighParameterId,
                                ana::corr::defaultHighCoefficient);
    const auto* ranges = processor.getParameters().getRawParameterValue(PluginProcessor::corrRangesVisibleParameterId);
    const auto* cursor = processor.getParameters().getRawParameterValue(PluginProcessor::corrCursorReadoutParameterId);
    const auto showRanges = ranges == nullptr || ranges->load(std::memory_order_relaxed) >= 0.5f;
    for (auto* control : std::array<ParameterControl*, 4> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl })
        control->setVisible(showRanges);
    cursorReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    cursorVerticalReadoutLabel.setVisible(cursor == nullptr || cursor->load(std::memory_order_relaxed) >= 0.5f);
    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    const auto frequencyScale = readCorrFrequencyScale(processor);
    frequencyRangeSlider.setRange(
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, std::min(lowFrequency, highFrequency)),
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, std::max(lowFrequency, highFrequency)));
    corrRangeSlider.setRange(
        ana::corr::toInvertedNormalised(std::max(lowRange, highRange), frequencyMode),
        ana::corr::toInvertedNormalised(std::min(lowRange, highRange), frequencyMode));
}

void CorrView::refreshControls()
{
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrZoomControlsParameterId);
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
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
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

    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    frequencyRangeSlider.setVisible(showZoom);
    corrRangeSlider.setVisible(showZoom);
}

void CorrView::updateFrequencyRangeFromSlider()
{
    const auto frequencyScale = readCorrFrequencyScale(processor);
    frequencyLowControl.getSlider().setValue(ana::analyzer_frequency::frequencyAt(
                                                  frequencyScale,
                                                  ana::analyzer_frequency::minimumHz,
                                                  ana::analyzer_frequency::maximumHz,
                                                  frequencyRangeSlider.getRangeStart()),
                                              juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(ana::analyzer_frequency::frequencyAt(
                                                   frequencyScale,
                                                   ana::analyzer_frequency::minimumHz,
                                                   ana::analyzer_frequency::maximumHz,
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
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::corrZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    if (showZoom)
    {
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
        bounds.removeFromRight(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    }
    return bounds;
}
