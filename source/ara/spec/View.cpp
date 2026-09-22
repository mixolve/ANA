#include "View.h"
#include "shared/spec/Settings.h"
#include "shared/analyzer/DisplaySettings.h"
#include "AnalyzerViewUtilities.h"
#include "shared/analyzer/SpectrogramFrequencyScale.h"
#include "shared/shell/GraphColours.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace ana::ui::analyzer_detail;

namespace
{
constexpr float specSlopeReferenceFrequency = 632.0f;

juce::Colour readGraphColour(const PluginProcessor& processor, const char* parameterId,
                             const int defaultIndex) noexcept
{
    return ana::ui::graphColour(juce::roundToInt(
        readParameterValue(processor, parameterId, static_cast<float>(defaultIndex))));
}

float readGraphOpacity(const PluginProcessor& processor) noexcept
{
    return juce::jlimit(0.01f, 1.0f, readParameterValue(
        processor, PluginProcessor::specGraphOpacityParameterId, 100.0f) * 0.01f);
}

struct SpecFrequencyRange
{
    float low = ana::analyzer_frequency::minimumHz;
    float high = ana::analyzer_frequency::maximumHz;
};

SpecFrequencyRange readSpecFrequencyRange(const PluginProcessor& processor) noexcept
{
    const auto configuredLow = readParameterValue(
        processor, PluginProcessor::specLowParameterId,
        ana::analyzer_frequency::minimumHz);
    const auto configuredHigh = readParameterValue(
        processor, PluginProcessor::specHighParameterId,
        ana::analyzer_frequency::maximumHz);
    const auto low = juce::jlimit(
        ana::analyzer_frequency::minimumHz,
        ana::analyzer_frequency::maximumHz - ana::analyzer_frequency::minimumSpanHz,
        std::min(configuredLow, configuredHigh));
    return {
        low,
        juce::jlimit(low + ana::analyzer_frequency::minimumSpanHz, ana::analyzer_frequency::maximumHz,
                     std::max(configuredLow, configuredHigh))
    };
}

ana::analyzer_frequency::Scale readSpecFrequencyScale(const PluginProcessor& processor) noexcept
{
    return ana::analyzer_frequency::scaleFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::specFrequencyScaleParameterId,
        static_cast<float>(ana::analyzer_frequency::defaultScaleIndex))));
}

ana::spec::ColourMap readSpecColourMap(const PluginProcessor& processor) noexcept
{
    return ana::spec::colourMapFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::specMapColourMapParameterId,
        static_cast<float>(ana::spec::defaultColourMapIndex))));
}

struct SpecDisplayRange
{
    float low = ana::spec::defaultDisplayLowDecibels;
    float high = ana::spec::defaultDisplayHighDecibels;
    float slope = ana::spec::defaultSlopeDecibelsPerOctave;
};

SpecDisplayRange readSpecDisplayRange(const PluginProcessor& processor) noexcept
{
    const auto low = readParameterValue(
        processor, PluginProcessor::specRangeLowParameterId, ana::spec::defaultDisplayLowDecibels);
    return {
        low,
        std::max(low + ana::spec::minimumDisplaySpanDecibels,
                 readParameterValue(processor, PluginProcessor::specRangeHighParameterId,
                                    ana::spec::defaultDisplayHighDecibels)),
        readParameterValue(processor, PluginProcessor::specSlopeParameterId,
                           ana::spec::defaultSlopeDecibelsPerOctave)
    };
}

ana::spec::MonitorMode readSpecMonitorMode(const PluginProcessor& processor) noexcept
{
    const auto index = juce::jlimit(
        0, static_cast<int>(ana::spec::monitorModeCount) - 1,
        juce::roundToInt(readParameterValue(
            processor, PluginProcessor::specMonitorModeParameterId, 0.0f)));
    return static_cast<ana::spec::MonitorMode>(index);
}

ana::spec::SpecProcessor::DisplayType readSpecDisplayType(
    const PluginProcessor& processor, const char* parameterId) noexcept
{
    return readParameterValue(processor, parameterId, 0.0f) >= 0.5f
        ? ana::spec::SpecProcessor::DisplayType::maximum
        : ana::spec::SpecProcessor::DisplayType::average;
}

bool shouldUseSplitView(const ana::spec::MonitorMode mode) noexcept
{
    return ana::spec::supportsSplitView(mode);
}

std::pair<ana::spec::Channel, ana::spec::Channel>
specChannelsForMode(const ana::spec::MonitorMode mode) noexcept
{
    using Channel = ana::spec::Channel;
    using MonitorMode = ana::spec::MonitorMode;

    switch (mode)
    {
        case MonitorMode::stereo:    return { Channel::stereo, Channel::stereo };
        case MonitorMode::leftRight: return { Channel::left, Channel::right };
        case MonitorMode::left:      return { Channel::left, Channel::left };
        case MonitorMode::right:     return { Channel::right, Channel::right };
        case MonitorMode::midSide:   return { Channel::mid, Channel::side };
        case MonitorMode::mid:       return { Channel::mid, Channel::mid };
        case MonitorMode::side:      return { Channel::side, Channel::side };
    }

    return { Channel::stereo, Channel::stereo };
}

size_t araMapChannelIndex(const ana::spec::Channel channel) noexcept
{
    return ana::spec::channelIndex(channel);
}

juce::Rectangle<float> splitPaneForCursor(const juce::Rectangle<float> plotBounds,
                                          const float cursorY,
                                          const bool useSplitView) noexcept
{
    if (! useSplitView)
        return plotBounds;

    constexpr float dividerHeight = 1.0f;
    auto upperBounds = plotBounds;
    upperBounds.setHeight(std::max(1.0f,
        (plotBounds.getHeight() - dividerHeight) * 0.5f));
    auto lowerBounds = upperBounds.withY(upperBounds.getBottom() + dividerHeight);

    return cursorY < upperBounds.getBottom() + dividerHeight * 0.5f
        ? upperBounds : lowerBounds;
}

juce::String formatMapTime(const double seconds)
{
    const auto safeSeconds = std::max(0.0, seconds);
    const auto minutes = static_cast<int>(safeSeconds / 60.0);
    const auto secondsInMinute = safeSeconds - static_cast<double>(minutes) * 60.0;
    return juce::String::formatted("%02d:%06.3f", minutes, secondsInMinute);
}

juce::Colour spectrogramColour(const float normalisedLevel,
                               const ana::spec::ColourMap colourMap) noexcept
{
    const auto clampedLevel = juce::jlimit(0.0f, 1.0f, normalisedLevel);
    if (colourMap == ana::spec::ColourMap::grayscale)
        return juce::Colour::fromFloatRGBA(clampedLevel, clampedLevel, clampedLevel, 1.0f);

    static constexpr std::array<juce::uint32, 200> colours {
        0xff000000u, 0xff000003u, 0xff000006u, 0xff000009u, 0xff00000cu, 0xff00000fu, 0xff000012u, 0xff000015u,
        0xff000018u, 0xff00001au, 0xff01001eu, 0xff010021u, 0xff010024u, 0xff010027u, 0xff01002au, 0xff01002eu,
        0xff010031u, 0xff020035u, 0xff020039u, 0xff02003cu, 0xff02003fu, 0xff020042u, 0xff020046u, 0xff020049u,
        0xff02004du, 0xff030051u, 0xff030055u, 0xff030058u, 0xff04005cu, 0xff040060u, 0xff050064u, 0xff050068u,
        0xff06006cu, 0xff06006fu, 0xff060072u, 0xff060075u, 0xff060078u, 0xff07007au, 0xff07007du, 0xff07007eu,
        0xff080081u, 0xff080083u, 0xff080085u, 0xff090086u, 0xff090088u, 0xff09008bu, 0xff0a0092u, 0xff0b009au,
        0xff0d00a2u, 0xff0e00a9u, 0xff0f00b3u, 0xff1100bfu, 0xff1400d0u, 0xff1600e2u, 0xff1900f4u, 0xff1b00ffu,
        0xff1b00ffu, 0xff1900ffu, 0xff1603ffu, 0xff1218ffu, 0xff092bffu, 0xff003effu, 0xff0050ffu, 0xff0062ffu,
        0xff0075ffu, 0xff0088ffu, 0xff009affu, 0xff00a6ffu, 0xff00b2fcu, 0xff00bff7u, 0xff00cbf3u, 0xff00d6eeu,
        0xff00e3e9u, 0xff00efe4u, 0xff00fbdfu, 0xff00ffd5u, 0xff00ffc5u, 0xff00ffb3u, 0xff00ffa1u, 0xff00ff8eu,
        0xff00ff7bu, 0xff00ff66u, 0xff00ff51u, 0xff00ff37u, 0xff00ff0fu, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u,
        0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff00ff00u, 0xff02ff00u,
        0xff33ff00u, 0xff62ff00u, 0xff80ff00u, 0xff98ff00u, 0xffaeff00u, 0xffc3ff00u, 0xffd2fa00u, 0xffdcee00u,
        0xffe4e200u, 0xffecd600u, 0xfff4ca00u, 0xfffbbe00u, 0xffffb300u, 0xffffa600u, 0xffff9900u, 0xffff8900u,
        0xffff7900u, 0xffff6a00u, 0xffff5a00u, 0xffff4a00u, 0xffff3a00u, 0xffff2200u, 0xffff0200u, 0xffff0000u,
        0xffff0000u, 0xffff0000u, 0xffff0002u, 0xffff001bu, 0xffff0032u, 0xffff0043u, 0xffff0053u, 0xffff0062u,
        0xffff0070u, 0xffff007eu, 0xffff008cu, 0xffff0099u, 0xffff00a6u, 0xffff00b2u, 0xffff00bfu, 0xffff00cdu,
        0xffff00dcu, 0xffff00eeu, 0xffff00fdu, 0xffff00ffu, 0xffff00ffu, 0xffff0affu, 0xffff30ffu, 0xffff48ffu,
        0xffff5bffu, 0xffff6cffu, 0xffff7cffu, 0xffff8cffu, 0xffff9bffu, 0xffffa6ffu, 0xffffadffu, 0xffffb2ffu,
        0xffffb5ffu, 0xffffb8ffu, 0xffffbbffu, 0xffffbeffu, 0xffffc1ffu, 0xffffc2ffu, 0xffffc5ffu, 0xffffc6ffu,
        0xffffc9ffu, 0xffffcbffu, 0xffffcdffu, 0xffffcfffu, 0xffffd1ffu, 0xffffd2ffu, 0xffffd4ffu, 0xffffd6ffu,
        0xffffd8ffu, 0xffffdaffu, 0xffffdcffu, 0xffffdeffu, 0xffffdfffu, 0xffffe1ffu, 0xffffe2ffu, 0xffffe4ffu,
        0xffffe6ffu, 0xffffe8ffu, 0xffffeaffu, 0xffffebffu, 0xffffedffu, 0xffffefffu, 0xfffff1ffu, 0xfffff3ffu,
        0xfffff5ffu, 0xfffff8ffu, 0xfffff9ffu, 0xfffffaffu, 0xfffffaffu, 0xfffffaffu, 0xfffffaffu, 0xfffffaffu,
        0xfffffaffu, 0xfffffbffu, 0xfffffbffu, 0xfffffbffu, 0xfffffcffu, 0xfffffdffu, 0xfffffeffu, 0xffffffffu
    };
    const auto scaled = clampedLevel * static_cast<float>(colours.size() - 1);
    const auto index = juce::jlimit(0, static_cast<int>(colours.size()) - 2,
                                   static_cast<int>(std::floor(scaled)));
    return juce::Colour(colours[static_cast<size_t>(index)]).interpolatedWith(
        juce::Colour(colours[static_cast<size_t>(index + 1)]),
        scaled - static_cast<float>(index));
}

}

SpecView::SpecView(PluginProcessor& processorRef)
    : processor(processorRef),
      frequencyLowControl(processorRef.getParameters(), PluginProcessor::specLowParameterId,
                          "LOW", [] (const double value) { return formatReadoutFrequency(value); }),
      frequencyHighControl(processorRef.getParameters(), PluginProcessor::specHighParameterId,
                           "HIGH", [] (const double value) { return formatReadoutFrequency(value); }),
      rangeLowControl(processorRef.getParameters(), PluginProcessor::specRangeLowParameterId,
                      "RANGE-LOW", [] (const double value) { return formatReadoutLevel(value); }),
      rangeHighControl(processorRef.getParameters(), PluginProcessor::specRangeHighParameterId,
                       "RANGE-HIGH", [] (const double value) { return formatReadoutLevel(value); })
{
    for (auto* component : std::array<juce::Component*, 6> {
             &frequencyLowControl, &frequencyHighControl, &rangeLowControl, &rangeHighControl,
             &frequencyRangeSlider, &magnitudeRangeSlider })
        addAndMakeVisible(*component);
    addAndMakeVisible(mapTimeRangeSlider);

    configureCursorReadoutLabel(cursorReadoutLabel);
    addAndMakeVisible(cursorReadoutLabel);

    configureCursorReadoutLabel(cursorNoteReadoutLabel);
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
    if (const auto analysisResult = processor.getAraAnalysisResult())
        displayedAraRevision = analysisResult->revision;
    araSpectrogramImage = processor.getCachedAraSpectrogramImage();

    startTimerHz(60);
}

size_t SpecView::addSnapshotSlot()
{
    snapshots.emplace_back();
    return snapshots.size() - 1;
}

bool SpecView::removeSnapshotSlot(const size_t snapshotIndex)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    snapshots.erase(snapshots.begin() + static_cast<std::ptrdiff_t>(snapshotIndex));
    repaint();
    return true;
}

bool SpecView::captureSnapshot(const size_t snapshotIndex)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    SpectrumSnapshot captured;
    captured.colour = snapshots[snapshotIndex].colour;
    captured.gainDb = snapshots[snapshotIndex].gainDb;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);

    const auto useSplitView = shouldUseSplitView(monitorMode);
    const auto* secondGraphEnabled = processor.getParameters().getRawParameterValue(
        PluginProcessor::specSecondGraphParameterId);
    captured.drawSecondGraph = useSplitView || secondGraphEnabled == nullptr
        || secondGraphEnabled->load(std::memory_order_relaxed) >= 0.5f;

    const auto* displayedSpec = &processor.getSpecProcessor();
    captured.sampleRate = displayedSpec->getSampleRate();
    if (const auto analysisResult = processor.getAraAnalysisResult();
        analysisResult != nullptr && analysisResult->spec != nullptr)
    {
        displayedSpec = analysisResult->spec.get();
        captured.sampleRate = analysisResult->specSampleRate;
    }

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

void SpecView::setSnapshotVisible(const size_t snapshotIndex, const bool shouldBeVisible)
{
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].visible == shouldBeVisible)
        return;

    snapshots[snapshotIndex].visible = shouldBeVisible;
    repaint();
}

void SpecView::setSnapshotColour(const size_t snapshotIndex, const juce::Colour colour)
{
    if (snapshotIndex >= snapshots.size())
        return;

    if (snapshots[snapshotIndex].colour == colour)
        return;

    snapshots[snapshotIndex].colour = colour;
    repaint();
}

void SpecView::setSnapshotGain(const size_t snapshotIndex, const float gainDb)
{
    if (snapshotIndex >= snapshots.size())
        return;

    const auto clampedGain = juce::jlimit(snapshotGainMinimumDb, snapshotGainMaximumDb, gainDb);
    if (std::abs(snapshots[snapshotIndex].gainDb - clampedGain) < 0.0001f)
        return;

    snapshots[snapshotIndex].gainDb = clampedGain;
    repaint();
}

bool SpecView::writeSnapshot(const size_t snapshotIndex, juce::OutputStream& output) const
{
    if (snapshotIndex >= snapshots.size())
        return false;

    const auto& snapshot = snapshots[snapshotIndex];
    const auto expectedBinCount = snapshot.fftSize > 0
        ? static_cast<size_t>(snapshot.fftSize / 2 + 1)
        : 0;
    if (! snapshot.hasData
        || ! ana::fft::StereoFftStream::isSupportedFftSize(snapshot.fftSize)
        || ! std::isfinite(snapshot.sampleRate) || snapshot.sampleRate <= 0.0
        || ! std::isfinite(snapshot.gainDb) || snapshot.gainDb < snapshotGainMinimumDb || snapshot.gainDb > snapshotGainMaximumDb
        || snapshot.primary.size() != expectedBinCount
        || (snapshot.drawSecondGraph ? snapshot.secondary.size() != expectedBinCount
                                    : ! snapshot.secondary.empty()))
        return false;

    const auto writeValues = [&output] (const std::vector<float>& values)
    {
        if (values.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;

        if (! output.writeInt(static_cast<int>(values.size())))
            return false;

        for (const auto value : values)
            if (! std::isfinite(value) || ! output.writeFloat(value))
                return false;

        return true;
    };

    return output.writeInt(snapshot.fftSize)
        && output.writeDouble(snapshot.sampleRate)
        && output.writeByte(snapshot.drawSecondGraph ? 1 : 0)
        && output.writeInt(static_cast<int>(snapshot.colour.getARGB()))
        && output.writeByte(snapshot.visible ? 1 : 0)
        && output.writeFloat(snapshot.gainDb)
        && writeValues(snapshot.primary)
        && writeValues(snapshot.secondary);
}

bool SpecView::readSnapshot(const size_t snapshotIndex, juce::InputStream& input)
{
    if (snapshotIndex >= snapshots.size())
        return false;

    constexpr juce::int64 fixedHeaderSize = 4 + 8 + 1 + 4 + 1 + 4;
    if (input.getNumBytesRemaining() < fixedHeaderSize)
        return false;

    SpectrumSnapshot loaded;
    loaded.fftSize = input.readInt();
    loaded.sampleRate = input.readDouble();
    const auto drawSecondGraph = input.readByte();
    loaded.colour = juce::Colour(static_cast<juce::uint32>(input.readInt()));
    const auto visible = input.readByte();
    loaded.gainDb = input.readFloat();

    const auto isBooleanByte = [] (const char value) noexcept
    {
        return value == 0 || value == 1;
    };
    if (! isBooleanByte(drawSecondGraph) || ! isBooleanByte(visible))
        return false;

    loaded.drawSecondGraph = drawSecondGraph == 1;
    loaded.visible = visible == 1;

    if (! ana::fft::StereoFftStream::isSupportedFftSize(loaded.fftSize)
        || ! std::isfinite(loaded.gainDb) || loaded.gainDb < snapshotGainMinimumDb || loaded.gainDb > snapshotGainMaximumDb
        || ! std::isfinite(loaded.sampleRate) || loaded.sampleRate <= 0.0)
        return false;

    const auto expectedBinCount = static_cast<size_t>(loaded.fftSize / 2 + 1);
    const auto readValues = [&input, expectedBinCount] (std::vector<float>& values, const bool required)
    {
        const auto count = input.readInt();
        const auto expectedCount = required ? expectedBinCount : 0;
        if (count < 0 || static_cast<size_t>(count) != expectedCount
            || input.getNumBytesRemaining() < static_cast<juce::int64>(count) * 4)
            return false;

        values.resize(expectedCount);
        for (auto& value : values)
        {
            value = input.readFloat();
            if (! std::isfinite(value))
                return false;
        }
        return true;
    };

    if (! readValues(loaded.primary, true)
        || ! readValues(loaded.secondary, loaded.drawSecondGraph)
        || input.getNumBytesRemaining() != 0)
        return false;

    loaded.hasData = true;
    snapshots[snapshotIndex] = std::move(loaded);
    repaint();
    return true;
}

bool SpecView::hasSnapshotData(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() && snapshots[snapshotIndex].hasData;
}

bool SpecView::isSnapshotVisible(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() && snapshots[snapshotIndex].visible;
}

juce::Colour SpecView::getSnapshotColour(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size()
        ? snapshots[snapshotIndex].colour
        : juce::Colours::white;
}

float SpecView::getSnapshotGain(const size_t snapshotIndex) const noexcept
{
    return snapshotIndex < snapshots.size() ? snapshots[snapshotIndex].gainDb : 0.0f;
}


void SpecView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto plotBounds = getPlotBounds();
    if (plotBounds.isEmpty())
        return;

    if (viewMode == ViewMode::map)
    {
        const auto& image = araSpectrogramImage;
        if (image.isValid())
            graphics.drawImage(image, plotBounds);

        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = shouldUseSplitView(monitorMode);
        if (useSplitView)
        {
            graphics.setColour(ana::ui::white);
            graphics.fillRect(plotBounds.getX(), plotBounds.getCentreY(),
                              plotBounds.getWidth(), 1.0f);
        }

        const auto* cursorReadout = processor.getParameters().getRawParameterValue(
            PluginProcessor::specCursorReadoutParameterId);
        if (cursorInside
            && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
        {
            const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);
            graphics.setColour(ana::ui::white.withAlpha(0.65f));
            graphics.drawLine(cursorPane.getX(), cursorPosition.y,
                              cursorPane.getRight(), cursorPosition.y, 0.5f);
            graphics.drawLine(cursorPosition.x, plotBounds.getY(),
                              cursorPosition.x, plotBounds.getBottom(), 0.5f);
        }
        return;
    }

    int fftSize = 0;
    auto sampleRate = processor.getSpecProcessor().getSampleRate();

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = shouldUseSplitView(monitorMode);
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
    if (const auto analysisResult = processor.getAraAnalysisResult();
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
                drawSpec(graphics, snapshot.secondary, snapshot.fftSize, snapshot.sampleRate,
                         lowerBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
            drawSpec(graphics, snapshot.primary, snapshot.fftSize, snapshot.sampleRate,
                     upperBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
        }

        graphics.setColour(ana::ui::white);
        graphics.fillRect(plotBounds.getX(), upperBounds.getBottom(), plotBounds.getWidth(), dividerHeight);
        drawSpec(graphics, secondarySpec, fftSize, sampleRate, lowerBounds,
                     secondColour, secondColour.withAlpha(graphOpacity));
        drawSpec(graphics, primarySpec, fftSize, sampleRate, upperBounds,
                     firstColour, firstColour.withAlpha(graphOpacity));
    }
    else
    {
        for (const auto& snapshot : snapshots)
        {
            if (! snapshot.visible || ! snapshot.hasData)
                continue;

            if (snapshot.drawSecondGraph)
                drawSpec(graphics, snapshot.secondary, snapshot.fftSize, snapshot.sampleRate,
                         plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
            drawSpec(graphics, snapshot.primary, snapshot.fftSize, snapshot.sampleRate,
                     plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
        }

        if (drawSecondGraph)
            drawSpec(graphics, secondarySpec, fftSize, sampleRate, plotBounds,
                         secondColour, secondColour.withAlpha(graphOpacity));
        drawSpec(graphics, primarySpec, fftSize, sampleRate, plotBounds,
                     firstColour, firstColour.withAlpha(graphOpacity));
    }

    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::specCursorReadoutParameterId);
    if (cursorInside && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f))
    {
        const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);
        graphics.setColour(ana::ui::white);
        graphics.drawLine(cursorPosition.x, plotBounds.getY(), cursorPosition.x, plotBounds.getBottom(), 0.5f);
        graphics.drawLine(cursorPane.getX(), cursorPosition.y, cursorPane.getRight(), cursorPosition.y, 0.5f);
    }
}

void SpecView::resized()
{
    const auto plotBounds = getPlotBounds().toNearestInt();
    const auto readVisibility = [this] (const char* parameterId)
    {
        const auto* value = processor.getParameters().getRawParameterValue(parameterId);
        return value == nullptr || value->load(std::memory_order_relaxed) >= 0.5f;
    };
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto araMap = mapMode;
    const auto showCursor = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorReadoutParameterId);
    const auto showMonitor = readVisibility(PluginProcessor::specMonitorControlsParameterId);
    const auto showZoomSetting = readVisibility(PluginProcessor::specZoomControlsParameterId);
    const auto showHorizontalZoom = showZoomSetting && (freqMode || araMap);
    const auto showVerticalZoom = showZoomSetting && (freqMode || mapMode);
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
    if (mapMode)
    {
        const auto frequencyFits = showCursor
            && topControls.remaining().getWidth() >= mapCursorFrequencyWidth;
        cursorVerticalReadoutLabel.setBounds(frequencyFits
            ? topControls.takeLeft(mapCursorFrequencyWidth) : juce::Rectangle<int>());
        const auto timeFits = showCursor
            && topControls.remaining().getWidth() >= mapCursorTimeWidth;
        cursorReadoutLabel.setBounds(timeFits
            ? topControls.takeLeft(mapCursorTimeWidth) : juce::Rectangle<int>());
        cursorNoteReadoutLabel.setBounds({});
    }
    else
    {
        cursorReadoutLabel.setBounds(showCursor
            ? topControls.takeLeft(frequencyReadoutWidth) : juce::Rectangle<int>());
        cursorNoteReadoutLabel.setBounds(showCursor
            ? topControls.takeLeft(ana::ui::textControlWidth(4)) : juce::Rectangle<int>());
        const auto cursorVerticalReadoutFits = showCursor
            && topControls.remaining().getWidth() >= levelReadoutWidth;
        cursorVerticalReadoutLabel.setBounds(cursorVerticalReadoutFits
            ? topControls.takeLeft(levelReadoutWidth) : juce::Rectangle<int>());
    }

    ana::ui::FixedGapRow optionalControls(topControls.remaining());
    for (auto& button : monitorButtons)
        button->setBounds(showMonitor
            ? optionalControls.takeLeft(button->getPreferredWidth()) : juce::Rectangle<int>());

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
        frequencyHighControl.setBounds(graphRight - frequencyReadoutWidth - levelReadoutWidth
                                           - ana::ui::gap.pixels(), readoutY,
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
        if (araMap)
        {
            mapTimeStartReadoutLabel.setBounds(plotBounds.getX(), readoutY,
                                               timeReadoutWidth, ana::ui::controlHeight);
            mapTimeEndReadoutLabel.setBounds(graphRight - frequencyReadoutWidth
                                                 - ana::ui::gap.pixels() - timeReadoutWidth,
                                             readoutY, timeReadoutWidth,
                                             ana::ui::controlHeight);
        }
        else
        {
            mapTimeStartReadoutLabel.setBounds({});
            mapTimeEndReadoutLabel.setBounds({});
        }
    }

    frequencyRangeSlider.setBounds(showHorizontalZoom && freqMode
        ? juce::Rectangle<int>(0, getHeight() - bandRangeSliderHeight,
                               getWidth(), bandRangeSliderHeight)
        : juce::Rectangle<int>());
    mapTimeRangeSlider.setBounds(showHorizontalZoom && araMap
        ? juce::Rectangle<int>(0, getHeight() - bandRangeSliderHeight,
                               getWidth(), bandRangeSliderHeight)
        : juce::Rectangle<int>());
    magnitudeRangeSlider.setBounds(showVerticalZoom
        ? juce::Rectangle<int>(getWidth() - bandZoomSliderWidth, 0,
                               bandZoomSliderWidth,
                               getHeight() - (showHorizontalZoom
                                   ? bandRangeSliderHeight + ana::ui::gap.pixels() : 0))
        : juce::Rectangle<int>());

    if (mapMode)
    {
        {
            const auto analysisGeometryChanged = ! araSpectrogramImage.isValid()
                || araSpectrogramImage.getWidth() != plotBounds.getWidth()
                || araSpectrogramImage.getHeight() != plotBounds.getHeight();

            // Cache geometry follows the plot, but always spans the full source duration.
            if (analysisGeometryChanged
                && processor.getAnalyzerPageState() == ana::AnalyzerPage::spec)
                processor.requestAraAnalysis(getAraMapColumnCount(), false, true,
                                                 getAraMapRowCount());

            if (analysisGeometryChanged)
            {
                if (! araSpectrogramImage.isValid())
                    rebuildAraSpectrogramImage();
                else
                    scheduleMapImageRebuild();
            }
        }
    }

    syncRangeSliders();
    refreshControls();
}

void SpecView::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
}

void SpecView::mouseMove(const juce::MouseEvent& event)
{
    const auto nextCursorInside = getPlotBounds().contains(event.position);
    if (cursorInside == nextCursorInside && cursorPosition == event.position)
        return;

    cursorInside = nextCursorInside;
    cursorPosition = event.position;
    updateCursorReadouts();
    repaint();
}

void SpecView::updateCursorReadouts()
{
    if ((cursorInside || lastCursorFrequency > 0.0f) && ! getPlotBounds().isEmpty())
    {
        const auto plotBounds = getPlotBounds();
        const auto frequencyRange = readSpecFrequencyRange(processor);
        const auto lowFrequency = frequencyRange.low;
        const auto highFrequency = frequencyRange.high;
        const auto monitorMode = readSpecMonitorMode(processor);
        const auto useSplitView = shouldUseSplitView(monitorMode);
        const auto cursorPane = splitPaneForCursor(plotBounds, cursorPosition.y, useSplitView);

        if (viewMode == ViewMode::map)
        {
            // In SPLIT, map cursor Y within the hovered pane's full frequency axis.
            const auto normalisedY = juce::jlimit(0.0f, 1.0f,
                (cursorPosition.y - cursorPane.getY()) / std::max(1.0f, cursorPane.getHeight()));
            lastCursorFrequency = ana::analyzer_frequency::frequencyAt(
                readSpecFrequencyScale(processor), lowFrequency, highFrequency,
                1.0f - normalisedY);
            cursorVerticalReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency),
                                               juce::dontSendNotification);

            const auto normalisedX = juce::jlimit(0.0f, 1.0f,
                (cursorPosition.x - plotBounds.getX()) / plotBounds.getWidth());
            if (const auto analysisResult = processor.getAraAnalysisResult();
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
        lastCursorFrequency = ana::analyzer_frequency::frequencyAt(
            readSpecFrequencyScale(processor), lowFrequency, highFrequency, normalisedX);
        cursorReadoutLabel.setText(formatReadoutFrequency(lastCursorFrequency), juce::dontSendNotification);
        static constexpr std::array<const char*, 12> noteNames {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        const auto midiNote = juce::roundToInt(69.0 + 12.0 * std::log2(lastCursorFrequency / 440.0f));
        const auto noteIndex = (midiNote % 12 + 12) % 12;
        cursorNoteReadoutLabel.setText(juce::String(noteNames[static_cast<size_t>(noteIndex)])
                                           + juce::String(midiNote / 12 - 1),
                                       juce::dontSendNotification);
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

void SpecView::mouseExit(const juce::MouseEvent&)
{
    if (! cursorInside)
        return;

    cursorInside = false;
    repaint();
}

juce::Rectangle<float> SpecView::getPlotBounds() const noexcept
{
    auto bounds = getLocalBounds().toFloat();
    const auto* zoom = processor.getParameters().getRawParameterValue(
        PluginProcessor::specZoomControlsParameterId);
    const auto showZoom = zoom == nullptr || zoom->load(std::memory_order_relaxed) >= 0.5f;
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto showHorizontalZoom = showZoom && (freqMode || mapMode);
    const auto showVerticalZoom = showZoom && (freqMode || mapMode);
    if (showHorizontalZoom)
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (showVerticalZoom)
        bounds.removeFromRight(static_cast<float>(bandZoomSliderWidth + ana::ui::gap.pixels()));
    return bounds;
}

size_t SpecView::getAraMapColumnCount() const noexcept
{
    // Time Overlap oversamples the plot-width base raster; it is not FFT-window overlap.
    return static_cast<size_t>(std::max(1,
        static_cast<int>(std::ceil(getPlotBounds().getWidth()))));
}

size_t SpecView::getAraMapRowCount() const noexcept
{
    // Keep analysis geometry independent of monitor mode. All channel variants are
    // calculated in one pass, so switching ST/LR/L/R/MS/M/S is render-only.
    const auto height = std::max(1, static_cast<int>(std::ceil(getPlotBounds().getHeight())));
    return static_cast<size_t>(std::max(
        static_cast<int>(ana::ara::SpectrogramMap::minimumRowCount), height));
}


void SpecView::timerCallback()
{
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
            processor.requestAraAnalysis(getAraMapColumnCount(), false, true,
                                             getAraMapRowCount());

            if (updateAraRevision(processor, displayedAraRevision))
            {
                scheduleMapImageRebuild();
                repaint();
                if (onAraUpdateStatus)
                    onAraUpdateStatus("UPDATED");
            }

            const auto useSplit = shouldUseSplitView(monitorMode);
            if (renderedAraMonitorMode != ana::spec::monitorModeIndex(monitorMode)
                || renderedAraSplitView != useSplit)
                scheduleMapImageRebuild();

            if (mapImageRebuildPending)
            {
                mapImageRebuildPending = false;
                rebuildAraSpectrogramImage();
            }
        }
        else
        {
            processor.requestAraAnalysis(size_t { 1 });
            if (updateAraRevision(processor, displayedAraRevision))
            {
                repaint();
                if (onAraUpdateStatus)
                    onAraUpdateStatus("UPDATED");
            }
        }
        return;
    }
}

void SpecView::refreshControls()
{
    const auto readValue = [this] (const char* parameterId, const float fallback)
    {
        if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto araMap = mapMode;
    const auto showMonitor = readValue(PluginProcessor::specMonitorControlsParameterId, 1.0f) >= 0.5f;
    const auto showZoom = readValue(PluginProcessor::specZoomControlsParameterId, 1.0f) >= 0.5f;
    const auto showRanges = readValue(PluginProcessor::specRangesVisibleParameterId, 1.0f) >= 0.5f;
    const auto showCursor = readValue(PluginProcessor::specCursorReadoutParameterId, 1.0f) >= 0.5f;
    freqButton.setToggleState(freqMode, juce::dontSendNotification);
    mapButton.setToggleState(mapMode, juce::dontSendNotification);
    const auto monitorMode = readSpecMonitorMode(processor);
    const auto modeIndex = ana::spec::monitorModeIndex(monitorMode);
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor && ! monitorButtons[index]->getBounds().isEmpty());
        monitorButtons[index]->setToggleState(static_cast<int>(index) == modeIndex, juce::dontSendNotification);
    }
    frequencyRangeSlider.setVisible(showZoom && freqMode);
    mapTimeRangeSlider.setVisible(showZoom && araMap);
    magnitudeRangeSlider.setVisible(showZoom && (freqMode || mapMode));

    frequencyLowControl.setVisible((freqMode || mapMode) && showRanges);
    frequencyHighControl.setVisible((freqMode || mapMode) && showRanges);
    rangeLowControl.setVisible(freqMode && showRanges);
    rangeHighControl.setVisible(freqMode && showRanges);
    mapTimeStartReadoutLabel.setVisible(araMap && showRanges);
    mapTimeEndReadoutLabel.setVisible(araMap && showRanges);
    cursorReadoutLabel.setVisible((freqMode || mapMode) && showCursor);
    cursorNoteReadoutLabel.setVisible(freqMode && showCursor);
    cursorVerticalReadoutLabel.setVisible((freqMode || mapMode) && showCursor);
}

juce::String SpecView::getSettingsViewModeName() const
{
    return viewMode == ViewMode::map ? "MAP" : "FREQ";
}

void SpecView::setViewMode(const ViewMode nextViewMode)
{
    if (viewMode == nextViewMode)
        return;

    viewMode = nextViewMode;
    processor.setSpecMapView(viewMode == ViewMode::map);
    cursorInside = false;
    if (viewMode == ViewMode::map)
    {
        if (processor.getAnalyzerPageState() == ana::AnalyzerPage::spec)
            processor.requestAraAnalysis(getAraMapColumnCount(), false, true,
                                             getAraMapRowCount());
        scheduleMapImageRebuild();
    }
    resized();
    repaint();
}

void SpecView::scheduleMapImageRebuild() noexcept
{
    mapImageRebuildPending = true;
}


void SpecView::rebuildAraSpectrogramImage()
{
    if (viewMode != ViewMode::map)
        return;

    // Preserve the last completed raster across REAPER editor-peer recreation.
    if (! araSpectrogramImage.isValid())
        araSpectrogramImage = processor.getCachedAraSpectrogramImage();

    const auto analysisResult = processor.getAraAnalysisResult();

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
        juce::Colour::fromFloatRGBA(0.01f, 0.015f, 0.02f, 1.0f));

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
    const auto firstIndex = araMapChannelIndex(firstChannel);
    const auto secondIndex = araMapChannelIndex(secondChannel);
    const auto useSplitView = shouldUseSplitView(monitorMode);
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
        && firstIndex != 0 && secondIndex != 0
        && map.columnCount == static_cast<size_t>(width)
        && map.rowCount == static_cast<size_t>(paneHeight)
        && (! useSplitView || height % 2 == 0)
        && juce::approximatelyEqual(mapTimeRangeStart, 0.0f)
        && juce::approximatelyEqual(mapTimeRangeEnd, 1.0f)
        && frequencyScale == ana::analyzer_frequency::Scale::logarithmic
        && juce::approximatelyEqual(lowFrequency, ana::analyzer_frequency::minimumHz)
        && juce::approximatelyEqual(highFrequency, ana::analyzer_frequency::maximumHz);

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
            const auto centreFrequency = ana::analyzer_frequency::frequencyAt(
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

        araSpectrogramImage = std::move(nextSpectrogramImage);
        processor.setCachedAraSpectrogramImage(araSpectrogramImage);
        renderedAraMonitorMode = ana::spec::monitorModeIndex(monitorMode);
        renderedAraSplitView = useSplitView;
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
        bool usePeak = false;
        bool valid = false;
    };

    std::vector<FrequencyLookup> frequencyForY(static_cast<size_t>(height));
    std::vector<bool> useSecondChannelForY(static_cast<size_t>(height), false);

    const auto sourceRowForFrequency = [&map] (const float frequency)
    {
        const auto clampedFrequency = juce::jlimit(
            ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz,
            frequency);
        const auto normalised = ana::spectrogram_frequency::normalisedForFrequency(
            ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz,
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
        const auto centreFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, centreNormalised);
        const auto upperFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, topNormalised);
        const auto lowerFrequency = ana::analyzer_frequency::frequencyAt(
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
    std::vector<int> previousPopulated(map.columnCount, -1);
    std::vector<int> nextPopulated(map.columnCount, -1);
    auto populatedColumn = -1;
    for (size_t column = 0; column < map.columnCount; ++column)
    {
        if (map.columnFrameCounts[column] != 0)
            populatedColumn = static_cast<int>(column);
        previousPopulated[column] = populatedColumn;
    }
    populatedColumn = -1;
    for (size_t column = map.columnCount; column-- > 0;)
    {
        if (map.columnFrameCounts[column] != 0)
            populatedColumn = static_cast<int>(column);
        nextPopulated[column] = populatedColumn;
    }

    const auto framePositionForTime = [&map] (const float normalisedTime)
    {
        // Cached columns are pixel-centred at (i + 0.5) / N.
        return juce::jlimit(0.0f, static_cast<float>(map.columnCount - 1),
            juce::jlimit(0.0f, 1.0f, normalisedTime)
                * static_cast<float>(map.columnCount) - 0.5f);
    };

    const auto findTimeBracket = [&map, &previousPopulated, &nextPopulated,
                                  &framePositionForTime] (const float normalisedTime)
    {
        TimeLookup lookup;
        const auto framePosition = framePositionForTime(normalisedTime);
        const auto floorColumn = juce::jlimit(0, static_cast<int>(map.columnCount) - 1,
            static_cast<int>(std::floor(framePosition)));
        const auto ceilColumn = std::min(static_cast<int>(map.columnCount) - 1, floorColumn + 1);
        auto firstColumn = previousPopulated[static_cast<size_t>(floorColumn)];
        auto secondColumn = nextPopulated[static_cast<size_t>(ceilColumn)];
        if (firstColumn < 0)
            firstColumn = secondColumn;
        if (secondColumn < 0)
            secondColumn = firstColumn;
        if (firstColumn < 0 || secondColumn < 0)
            return lookup;

        lookup.column0 = static_cast<size_t>(firstColumn);
        lookup.column1 = static_cast<size_t>(secondColumn);
        lookup.mix = firstColumn == secondColumn ? 0.0f : juce::jlimit(
            0.0f, 1.0f,
            (framePosition - static_cast<float>(firstColumn))
                / static_cast<float>(secondColumn - firstColumn));
        lookup.valid = true;
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
            const auto first = std::max(0, static_cast<int>(std::ceil(firstCoord)));
            const auto last = std::min(static_cast<int>(map.columnCount - 1),
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

    araSpectrogramImage = std::move(nextSpectrogramImage);
    processor.setCachedAraSpectrogramImage(araSpectrogramImage);
    renderedAraMonitorMode = ana::spec::monitorModeIndex(monitorMode);
    renderedAraSplitView = useSplitView;
    repaint();
}


void SpecView::syncRangeSliders()
{
    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto* rangesVisible = processor.getParameters().getRawParameterValue(
        PluginProcessor::specRangesVisibleParameterId);
    const auto* cursorReadout = processor.getParameters().getRawParameterValue(
        PluginProcessor::specCursorReadoutParameterId);
    const auto freqMode = viewMode == ViewMode::frequency;
    const auto mapMode = viewMode == ViewMode::map;
    const auto showRanges = rangesVisible == nullptr
        || rangesVisible->load(std::memory_order_relaxed) >= 0.5f;
    const auto shouldShowCursor = (freqMode || mapMode)
        && (cursorReadout == nullptr || cursorReadout->load(std::memory_order_relaxed) >= 0.5f);

    frequencyLowControl.setVisible((freqMode || mapMode) && showRanges);
    frequencyHighControl.setVisible((freqMode || mapMode) && showRanges);
    rangeLowControl.setVisible(freqMode && showRanges);
    rangeHighControl.setVisible(freqMode && showRanges);
    cursorReadoutLabel.setVisible(shouldShowCursor);
    cursorNoteReadoutLabel.setVisible(freqMode && shouldShowCursor);
    cursorVerticalReadoutLabel.setVisible(shouldShowCursor);
    mapTimeStartReadoutLabel.setVisible(mapMode && showRanges);
    mapTimeEndReadoutLabel.setVisible(mapMode && showRanges);
    updateCursorReadouts();
    updateMapTimeReadouts();

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, lowFrequency),
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, highFrequency));
    mapTimeRangeSlider.setRange(mapTimeRangeStart, mapTimeRangeEnd);
    if (mapMode)
    {
        const auto lower = ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale,
            ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, lowFrequency);
        const auto upper = ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale,
            ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, highFrequency);
        magnitudeRangeSlider.setRange(1.0f - upper, 1.0f - lower);
    }
    else
    {
        constexpr auto magnitudeSpan = ana::spec::maximumDisplayDecibels - ana::spec::minimumDisplayDecibels;
        magnitudeRangeSlider.setRange((ana::spec::maximumDisplayDecibels - std::max(lowRange, highRange)) / magnitudeSpan,
                                      (ana::spec::maximumDisplayDecibels - std::min(lowRange, highRange)) / magnitudeSpan);
    }
}

void SpecView::updateFrequencyRangeFromSlider()
{
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto lowFrequency = ana::analyzer_frequency::frequencyAt(
        frequencyScale, ana::analyzer_frequency::minimumHz,
        ana::analyzer_frequency::maximumHz, frequencyRangeSlider.getRangeStart());
    const auto highFrequency = ana::analyzer_frequency::frequencyAt(
        frequencyScale, ana::analyzer_frequency::minimumHz,
        ana::analyzer_frequency::maximumHz, frequencyRangeSlider.getRangeEnd());
    frequencyLowControl.getSlider().setValue(lowFrequency, juce::sendNotificationSync);
    frequencyHighControl.getSlider().setValue(highFrequency, juce::sendNotificationSync);
    repaint();
}

void SpecView::updateVerticalRangeFromSlider()
{
    if (viewMode == ViewMode::map)
    {
        const auto frequencyScale = readSpecFrequencyScale(processor);
        const auto highFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, 1.0f - magnitudeRangeSlider.getRangeStart());
        const auto lowFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, 1.0f - magnitudeRangeSlider.getRangeEnd());
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

    // Recolour on the refresh cadence, not every mouseDrag, to keep large maps responsive.
    if (viewMode == ViewMode::map)
        scheduleMapImageRebuild();
    repaint();
}

void SpecView::updateMapTimeRangeFromSlider()
{
    mapTimeRangeStart = mapTimeRangeSlider.getRangeStart();
    mapTimeRangeEnd = mapTimeRangeSlider.getRangeEnd();
    updateMapTimeReadouts();

    scheduleMapImageRebuild();
    repaint();
}

void SpecView::updateMapTimeReadouts()
{
    const auto analysisResult = processor.getAraAnalysisResult();
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


void SpecView::drawSpec(juce::Graphics& graphics,
                                const std::vector<float>& spec,
                                const int fftSize,
                                const double sampleRate,
                                const juce::Rectangle<float> plotBounds,
                                const juce::Colour lineColour,
                                const juce::Colour fillColour,
                                const bool allowFill,
                                const float gainDb) const
{
    if (fftSize <= 0 || spec.empty())
        return;

    const auto frequencyRange = readSpecFrequencyRange(processor);
    const auto lowFrequency = frequencyRange.low;
    const auto highFrequency = frequencyRange.high;
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyScale = readSpecFrequencyScale(processor);
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto columnCount = std::max(1, static_cast<int>(std::ceil(plotBounds.getWidth())));

    std::vector<float> columnSums(static_cast<size_t>(columnCount), 0.0f);
    std::vector<int> columnCounts(static_cast<size_t>(columnCount), 0);
    auto hasData = false;

    // Aggregate in power so logarithmic pixels are independent of their linear-bin count.
    for (size_t bin = 1; bin < spec.size(); ++bin)
    {
        const auto frequency = static_cast<float>(bin) * binFrequency;
        if (frequency < lowFrequency || frequency > highFrequency)
            continue;

        const auto normalisedFrequency = ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, lowFrequency, highFrequency, frequency);
        const auto octaveOffset = std::log2(frequency / specSlopeReferenceFrequency);
        const auto value = spec[bin] + slope * octaveOffset + gainDb;
        const auto column = juce::jlimit(0, columnCount - 1,
            static_cast<int>(std::floor(normalisedFrequency * static_cast<float>(columnCount))));
        const auto columnIndex = static_cast<size_t>(column);

        const auto gain = juce::Decibels::decibelsToGain(value, ana::spec::SpecProcessor::minimumDecibels);
        columnSums[columnIndex] += gain * gain;
        ++columnCounts[columnIndex];
        hasData = hasData || value > lowRange + 0.01f;
    }

    if (! hasData)
        return;

    const auto smoothing = readParameterValue(processor, PluginProcessor::specSmoothingParameterId, ana::analyzer_display::defaultSmoothingPercent);
    const auto smoothingRadius = ana::analyzer_display::smoothingRadius(smoothing);
    const auto sigma = ana::analyzer_display::smoothingSigma(smoothingRadius);
    juce::Path path;
    juce::Point<float> firstPoint;
    juce::Point<float> lastPoint;
    std::vector<juce::Point<int>> aliasedPoints;
    aliasedPoints.reserve(static_cast<size_t>(columnCount + 1));
    bool hasPoint = false;

    for (int column = 0; column < columnCount; ++column)
    {
        auto summedValue = 0.0f;
        auto summedWeight = 0.0f;
        const auto firstColumn = std::max(0, column - smoothingRadius);
        const auto lastColumn = std::min(columnCount - 1, column + smoothingRadius);
        for (int neighbour = firstColumn; neighbour <= lastColumn; ++neighbour)
        {
            const auto count = columnCounts[static_cast<size_t>(neighbour)];
            if (count == 0)
                continue;

            const auto distance = static_cast<float>(std::abs(neighbour - column));
            const auto weight = std::exp(-0.5f * distance * distance / (sigma * sigma));
            summedValue += columnSums[static_cast<size_t>(neighbour)] * weight;
            summedWeight += static_cast<float>(count) * weight;
        }

        if (summedWeight <= 0.0f)
            continue;

        const auto displayValue = juce::Decibels::gainToDecibels(
            std::sqrt(summedValue / summedWeight), lowRange);
        const auto normalisedLevel = juce::jlimit(0.0f, 1.0f,
            (displayValue - lowRange) / (highRange - lowRange));
        const auto point = juce::Point<float>(
            plotBounds.getX() + static_cast<float>(column) + 0.5f,
            plotBounds.getBottom() - normalisedLevel * plotBounds.getHeight());

        if (! hasPoint)
        {
            firstPoint = point.withX(plotBounds.getX());
            path.startNewSubPath(firstPoint);
            aliasedPoints.emplace_back(juce::roundToInt(firstPoint.x), juce::roundToInt(firstPoint.y));
            hasPoint = true;
        }
        else
        {
            path.lineTo(point);
            aliasedPoints.emplace_back(juce::roundToInt(point.x), juce::roundToInt(point.y));
        }
        lastPoint = point;
    }

    if (! hasPoint)
        return;

    const auto* antiAlias = processor.getParameters().getRawParameterValue(
        PluginProcessor::specAntiAliasParameterId);
    const auto shouldAntiAlias = antiAlias == nullptr
        || antiAlias->load(std::memory_order_relaxed) >= 0.5f;
    const auto fillBaseline = plotBounds.getBottom();

    const auto* filled = processor.getParameters().getRawParameterValue(
        PluginProcessor::specFilledDisplayParameterId);
    const auto shouldFill = allowFill
        && filled != nullptr
        && filled->load(std::memory_order_relaxed) >= 0.5f;

    if (shouldAntiAlias)
    {
        if (shouldFill)
        {
            auto fillPath = path;
            fillPath.lineTo(lastPoint.x, fillBaseline);
            fillPath.lineTo(firstPoint.x, fillBaseline);
            fillPath.closeSubPath();
            graphics.setColour(fillColour);
            graphics.fillPath(fillPath);
        }

        graphics.setColour(lineColour);
        graphics.strokePath(path, juce::PathStrokeType(1.0f));
        return;
    }

    // JUCE line primitives stay anti-aliased; draw 1px rectangles for a truly aliased OFF mode.
    juce::Graphics::ScopedSaveState saveState(graphics);
    graphics.reduceClipRegion(plotBounds.toNearestInt());

    const auto rasteriseLine = [&graphics] (juce::Point<int> from, juce::Point<int> to)
    {
        auto x0 = from.x;
        auto y0 = from.y;
        const auto x1 = to.x;
        const auto y1 = to.y;
        const auto dx = std::abs(x1 - x0);
        const auto sx = x0 < x1 ? 1 : -1;
        const auto dy = -std::abs(y1 - y0);
        const auto sy = y0 < y1 ? 1 : -1;
        auto error = dx + dy;

        for (;;)
        {
            graphics.fillRect(x0, y0, 1, 1);
            if (x0 == x1 && y0 == y1)
                break;

            const auto twiceError = error * 2;
            if (twiceError >= dy)
            {
                error += dy;
                x0 += sx;
            }
            if (twiceError <= dx)
            {
                error += dx;
                y0 += sy;
            }
        }
    };

    if (shouldFill && ! aliasedPoints.empty())
    {
        const auto baselineY = juce::roundToInt(fillBaseline);
        graphics.setColour(fillColour);

        const auto fillColumn = [&graphics, baselineY] (const int x, const int y)
        {
            const auto top = std::min(y, baselineY);
            const auto bottom = std::max(y, baselineY);
            graphics.fillRect(x, top, 1, bottom - top + 1);
        };

        fillColumn(aliasedPoints.front().x, aliasedPoints.front().y);
        for (size_t index = 1; index < aliasedPoints.size(); ++index)
        {
            const auto from = aliasedPoints[index - 1];
            const auto to = aliasedPoints[index];
            const auto xDistance = to.x - from.x;

            if (xDistance == 0)
            {
                fillColumn(to.x, to.y);
                continue;
            }

            const auto step = xDistance > 0 ? 1 : -1;
            for (auto x = from.x; x != to.x + step; x += step)
            {
                const auto proportion = static_cast<float>(x - from.x)
                    / static_cast<float>(xDistance);
                const auto y = juce::roundToInt(juce::jmap(proportion,
                                                           static_cast<float>(from.y),
                                                           static_cast<float>(to.y)));
                fillColumn(x, y);
            }
        }
    }

    graphics.setColour(lineColour);
    if (aliasedPoints.size() == 1)
    {
        graphics.fillRect(aliasedPoints.front().x, aliasedPoints.front().y, 1, 1);
        return;
    }

    for (size_t index = 1; index < aliasedPoints.size(); ++index)
        rasteriseLine(aliasedPoints[index - 1], aliasedPoints[index]);
}
