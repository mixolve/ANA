#include "View.h"
#include "shared/spec/Settings.h"
#include "shared/analyzer/DisplaySettings.h"
#include "AnalyzerViewUtilities.h"
#include "shared/analyzer/SpectrogramFrequencyScale.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace ana::ui::analyzer_detail;

namespace
{
constexpr float specSlopeReferenceFrequency = 632.0f;

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

bool shouldUseSplitView(const PluginProcessor& processor,
                        const ana::spec::MonitorMode mode,
                        const bool mapMode) noexcept
{
    if (! ana::spec::supportsSplitView(mode))
        return false;

    if (mapMode && mode == ana::spec::MonitorMode::leftRight)
        return true;

    const auto* split = processor.getParameters().getRawParameterValue(
        PluginProcessor::specSplitViewParameterId);
    return split != nullptr && split->load(std::memory_order_relaxed) >= 0.5f;
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
        case MonitorMode::delta:     return { Channel::delta, Channel::delta };
    }

    return { Channel::stereo, Channel::stereo };
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

juce::Colour spectrogramColour(const float normalisedLevel) noexcept
{
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
    const auto scaled = juce::jlimit(0.0f, 1.0f, normalisedLevel)
        * static_cast<float>(colours.size() - 1);
    const auto index = juce::jlimit(0, static_cast<int>(colours.size()) - 2,
                                   static_cast<int>(std::floor(scaled)));
    return juce::Colour(colours[static_cast<size_t>(index)]).interpolatedWith(
        juce::Colour(colours[static_cast<size_t>(index + 1)]),
        scaled - static_cast<float>(index));
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

    configureCursorReadoutLabel(cursorReadoutLabel);
    addAndMakeVisible(cursorReadoutLabel);

    configureCursorReadoutLabel(cursorNoteReadoutLabel);
    addAndMakeVisible(cursorNoteReadoutLabel);

    configureCursorReadoutLabel(cursorVerticalReadoutLabel);
    addAndMakeVisible(cursorVerticalReadoutLabel);


    viewMode = processor.isSpecMapView() ? ViewMode::map : ViewMode::frequency;
    processor.getSpecProcessor().setRtMapMode(viewMode == ViewMode::map);
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
        if (index == ana::spec::monitorModeIndex(ana::spec::MonitorMode::delta))
            button->setTooltip("DELTA");
        addAndMakeVisible(*button);
        monitorButtons[index] = std::move(button);
    }
    splitButton.setTooltip("SPLIT");
    splitButton.setClickingTogglesState(true);
    splitButton.onClick = [this]
    {
        if (viewMode == ViewMode::map
            && readSpecMonitorMode(processor) == ana::spec::MonitorMode::leftRight)
            return;

        if (auto* parameter = processor.getParameters().getParameter(PluginProcessor::specSplitViewParameterId))
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
    captured.deltaMode = monitorMode == ana::spec::MonitorMode::delta;

    const auto* split = processor.getParameters().getRawParameterValue(
        PluginProcessor::specSplitViewParameterId);
    const auto useSplitView = ana::spec::supportsSplitView(monitorMode)
        && split != nullptr
        && split->load(std::memory_order_relaxed) >= 0.5f;
    const auto* secondGraphEnabled = processor.getParameters().getRawParameterValue(
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
        && output.writeByte(snapshot.deltaMode ? 1 : 0)
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

    constexpr juce::int64 fixedHeaderSize = 4 + 8 + 1 + 1 + 4 + 1 + 4;
    if (input.getNumBytesRemaining() < fixedHeaderSize)
        return false;

    SpectrumSnapshot loaded;
    loaded.fftSize = input.readInt();
    loaded.sampleRate = input.readDouble();
    const auto drawSecondGraph = input.readByte();
    const auto deltaMode = input.readByte();
    loaded.colour = juce::Colour(static_cast<juce::uint32>(input.readInt()));
    const auto visible = input.readByte();
    loaded.gainDb = input.readFloat();

    const auto isBooleanByte = [] (const char value) noexcept
    {
        return value == 0 || value == 1;
    };
    if (! isBooleanByte(drawSecondGraph) || ! isBooleanByte(deltaMode) || ! isBooleanByte(visible))
        return false;

    loaded.drawSecondGraph = drawSecondGraph == 1;
    loaded.deltaMode = deltaMode == 1;
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

void SpecView::clearSpectrogram()
{
    rtMapWriteColumn = 0;
    std::fill(rtSpectrogramLevels.begin(), rtSpectrogramLevels.end(), ana::spec::SpecProcessor::minimumDecibels);
    if (spectrogramImage.isValid())
        spectrogramImage.clear(spectrogramImage.getBounds(), juce::Colours::black);
    repaint();
}

void SpecView::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
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
    const auto* split = processor.getParameters().getRawParameterValue(
        PluginProcessor::specSplitViewParameterId);
    const auto splitViewAvailable = ana::spec::supportsSplitView(monitorMode);
    const auto useSplitView = splitViewAvailable && split != nullptr
        && split->load(std::memory_order_relaxed) >= 0.5f;
    const auto* secondGraphEnabled = processor.getParameters().getRawParameterValue(
        PluginProcessor::specSecondGraphParameterId);
    const auto drawSecondGraph = useSplitView
        || secondGraphEnabled == nullptr
        || secondGraphEnabled->load(std::memory_order_relaxed) >= 0.5f;
    const auto firstType = readSpecDisplayType(processor, PluginProcessor::specFirstGraphTypeParameterId);
    const auto secondType = readSpecDisplayType(processor, PluginProcessor::specSecondGraphTypeParameterId);
    const auto copySpectra = [&] (const ana::spec::SpecProcessor& spec)
    {
        spec.copySpectrum(firstChannel, firstType, primarySpec, fftSize);
        if (drawSecondGraph)
            spec.copySpectrum(secondChannel, secondType, secondarySpec, fftSize);
    };
    copySpectra(processor.getSpecProcessor());

    const auto currentDeltaMode = monitorMode == ana::spec::MonitorMode::delta;
    if (useSplitView)
    {
        auto upperBounds = plotBounds;
        constexpr float dividerHeight = 1.0f;
        upperBounds.setHeight((plotBounds.getHeight() - dividerHeight) * 0.5f);
        auto lowerBounds = upperBounds.withY(upperBounds.getBottom() + dividerHeight);

        for (const auto& snapshot : snapshots)
        {
            if (! snapshot.visible || ! snapshot.hasData || snapshot.deltaMode != currentDeltaMode)
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
                     ana::ui::white, ana::ui::dark);
        drawSpec(graphics, primarySpec, fftSize, sampleRate, upperBounds,
                     ana::ui::white, ana::ui::light);
    }
    else
    {
        for (const auto& snapshot : snapshots)
        {
            if (! snapshot.visible || ! snapshot.hasData || snapshot.deltaMode != currentDeltaMode)
                continue;

            if (snapshot.drawSecondGraph)
                drawSpec(graphics, snapshot.secondary, snapshot.fftSize, snapshot.sampleRate,
                         plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
            drawSpec(graphics, snapshot.primary, snapshot.fftSize, snapshot.sampleRate,
                     plotBounds, snapshot.colour, ana::ui::dark, false, snapshot.gainDb);
        }

        if (drawSecondGraph)
            drawSpec(graphics, secondarySpec, fftSize, sampleRate, plotBounds,
                         ana::ui::white, ana::ui::dark);
        drawSpec(graphics, primarySpec, fftSize, sampleRate, plotBounds,
                     ana::ui::white, ana::ui::light);
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
    const auto showCursor = (freqMode || mapMode)
        && readVisibility(PluginProcessor::specCursorReadoutParameterId);
    const auto showMonitor = readVisibility(PluginProcessor::specMonitorControlsParameterId);
    const auto showZoomSetting = readVisibility(PluginProcessor::specZoomControlsParameterId);
    const auto showHorizontalZoom = showZoomSetting && freqMode;
    const auto showVerticalZoom = showZoomSetting && (freqMode || mapMode);
    constexpr int frequencyReadoutWidth = ana::ui::textControlWidth(8);
    constexpr int levelReadoutWidth = ana::ui::textControlWidth(7);
    constexpr int timeReadoutWidth = ana::ui::textControlWidth(10);
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
    splitButtonFits = showMonitor
        && optionalControls.remaining().getWidth() >= splitButton.getPreferredWidth();
    splitButton.setBounds(splitButtonFits
        ? optionalControls.takeLeft(splitButton.getPreferredWidth()) : juce::Rectangle<int>());

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
    }
    else if (mapMode)
    {
        frequencyLowControl.setBounds(graphRight - frequencyReadoutWidth, readoutY,
                                      frequencyReadoutWidth, ana::ui::controlHeight);
        rangeLowControl.setBounds({});
    }

    frequencyRangeSlider.setBounds(showHorizontalZoom && freqMode
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
        resetSpectrogramImage();
    }

    syncRangeSliders();
    refreshControls();
}

void SpecView::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != this || processor.getSpecProcessor().isFrozen())
        return;

    processor.clearSpecProcessor();
    if (viewMode == ViewMode::map)
        clearSpectrogram();
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
        const auto useSplitView = shouldUseSplitView(
            processor, monitorMode, viewMode == ViewMode::map);
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
            {
                const auto& fftSizes = ana::fft::StereoFftStream::supportedFftSizes;
                const auto fftIndex = juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                    juce::roundToInt(readParameterValue(
                        processor, PluginProcessor::specFftSizeParameterId,
                        static_cast<float>(ana::fft::StereoFftStream::defaultFftSizeIndex))));
                const auto fftSize = fftSizes[static_cast<size_t>(fftIndex)];
                const auto sampleRate = processor.getSpecProcessor().getSampleRate();
                // Cursor time uses the base raster hop; Time Overlap only adds intermediate analyses.
                const auto hopSize = ana::spec::SpecProcessor::mapBaseHopSizeForFftSize(fftSize);
                const auto secondsPerColumn = sampleRate > 0.0
                    ? static_cast<double>(hopSize) / sampleRate : 0.0;
                const auto historyColumns = std::max(0, spectrogramImage.getWidth() - 1);
                const auto ageSeconds = (1.0 - static_cast<double>(normalisedX))
                    * static_cast<double>(historyColumns) * secondsPerColumn;
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
    const auto showHorizontalZoom = showZoom && freqMode;
    const auto showVerticalZoom = showZoom && (freqMode || mapMode);
    if (showHorizontalZoom)
        bounds.removeFromBottom(static_cast<float>(bandRangeSliderHeight + ana::ui::gap.pixels()));
    if (showVerticalZoom)
        bounds.removeFromRight(static_cast<float>(bandZoomSliderWidth + ana::ui::gap.pixels()));
    return bounds;
}


void SpecView::timerCallback()
{
    syncRangeSliders();
    refreshControls();

    const auto currentClearRevision = processor.getSpecProcessor().getClearRevision();
    if (displayedClearRevision != currentClearRevision)
    {
        displayedClearRevision = currentClearRevision;
        clearSpectrogram();
    }

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto displayRange = readSpecDisplayRange(processor);
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto slope = displayRange.slope;
    const auto frequencyScale = readSpecFrequencyScale(processor);
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
    if (viewMode == ViewMode::map
        && (rangeChanged || slopeChanged || highQualityChanged || mapDirectionChanged
            || frequencyScaleChanged))
    {
        if (highQualityChanged || mapDirectionChanged || frequencyScaleChanged)
            clearSpectrogram();
        scheduleMapImageRebuild();
        renderedMapHighQuality = highQuality ? 1 : 0;
        renderedMapLeftToRight = mapLeftToRight ? 1 : 0;
        renderedMapFrequencyScale = static_cast<int>(frequencyScale);
    }

    if (viewMode == ViewMode::map && mapImageRebuildPending)
    {
        mapImageRebuildPending = false;
        rebuildRtSpectrogramImage();
        repaint();
    }

    const auto currentRevision = viewMode == ViewMode::map
        ? processor.getSpecProcessor().getMapRevision()
        : processor.getSpecProcessor().getRevision();
    if (displayedRevision != currentRevision)
    {
        displayedRevision = currentRevision;
        if (viewMode == ViewMode::map)
            appendSpectrogramFrame();
        repaint();
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
    const auto showMonitor = readValue(PluginProcessor::specMonitorControlsParameterId, 1.0f) >= 0.5f;
    const auto showZoom = readValue(PluginProcessor::specZoomControlsParameterId, 1.0f) >= 0.5f;
    const auto showRanges = readValue(PluginProcessor::specRangesVisibleParameterId, 1.0f) >= 0.5f;
    const auto showCursor = readValue(PluginProcessor::specCursorReadoutParameterId, 1.0f) >= 0.5f;
    freqButton.setToggleState(freqMode, juce::dontSendNotification);
    mapButton.setToggleState(mapMode, juce::dontSendNotification);
    const auto monitorMode = readSpecMonitorMode(processor);
    const auto modeIndex = ana::spec::monitorModeIndex(monitorMode);
    const auto splitAvailable = ana::spec::supportsSplitView(monitorMode);
    const auto splitForced = mapMode && monitorMode == ana::spec::MonitorMode::leftRight;
    for (size_t index = 0; index < monitorButtons.size(); ++index)
    {
        monitorButtons[index]->setVisible(showMonitor && ! monitorButtons[index]->getBounds().isEmpty());
        monitorButtons[index]->setToggleState(static_cast<int>(index) == modeIndex, juce::dontSendNotification);
    }
    splitButton.setVisible(showMonitor && splitButtonFits);
    splitButton.setEnabled(splitAvailable && ! splitForced);
    splitButton.setToggleState(splitAvailable
                                   && (splitForced
                                       || readValue(PluginProcessor::specSplitViewParameterId, 0.0f) >= 0.5f),
                               juce::dontSendNotification);
    frequencyRangeSlider.setVisible(showZoom && freqMode);
    magnitudeRangeSlider.setVisible(showZoom && (freqMode || mapMode));

    frequencyLowControl.setVisible((freqMode || mapMode) && showRanges);
    frequencyHighControl.setVisible((freqMode || mapMode) && showRanges);
    rangeLowControl.setVisible(freqMode && showRanges);
    rangeHighControl.setVisible(freqMode && showRanges);
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
    processor.getSpecProcessor().setRtMapMode(viewMode == ViewMode::map);
    cursorInside = false;
    if (viewMode == ViewMode::map)
    {
        resetSpectrogramImage();
    }
    resized();
    repaint();
}

void SpecView::scheduleMapImageRebuild() noexcept
{
    mapImageRebuildPending = true;
}

void SpecView::resetSpectrogramImage()
{
    const auto bounds = getPlotBounds().toNearestInt();
    const auto width = std::max(1, bounds.getWidth());
    const auto height = std::max(1, bounds.getHeight());

    if (spectrogramImage.isValid()
        && spectrogramImage.getWidth() == width
        && spectrogramImage.getHeight() == height
        && rtSpectrogramLevels.size() == static_cast<size_t>(width * height))
        return;

    const auto oldWidth = spectrogramImage.isValid() ? spectrogramImage.getWidth() : 0;
    const auto oldHeight = spectrogramImage.isValid() ? spectrogramImage.getHeight() : 0;
    auto oldLevels = std::move(rtSpectrogramLevels);
    rtSpectrogramLevels.assign(static_cast<size_t>(width * height), ana::spec::SpecProcessor::minimumDecibels);
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
                rtSpectrogramLevels[static_cast<size_t>(y * width + x)] =
                    oldLevels[static_cast<size_t>(sourceY * oldWidth + sourceX)];
            }
        }
    }

    spectrogramImage = juce::Image(juce::Image::RGB, width, height, true);
    rebuildRtSpectrogramImage();
}

void SpecView::rebuildRtSpectrogramImage()
{
    if (! spectrogramImage.isValid()
        || rtSpectrogramLevels.size()
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
        const auto frequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto rowOffset = static_cast<size_t>(y * width);
        for (int x = 0; x < width; ++x)
        {
            const auto rawValue = rtSpectrogramLevels[rowOffset + static_cast<size_t>(x)];
            const auto level = juce::jlimit(0.0f, 1.0f,
                (rawValue + slopeOffset - lowRange) / (highRange - lowRange));
            pixels.setPixelColour(x, y, spectrogramColour(level));
        }
    }

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
}


void SpecView::appendSpectrogramFrame()
{
    resetSpectrogramImage();
    if (! spectrogramImage.isValid())
        return;

    const auto monitorMode = readSpecMonitorMode(processor);
    const auto deltaMode = monitorMode == ana::spec::MonitorMode::delta;
    const auto [firstChannel, secondChannel] = specChannelsForMode(monitorMode);
    const auto useSplitView = shouldUseSplitView(processor, monitorMode, true);
    const auto useSecondSpectrum = ! deltaMode && secondChannel != firstChannel;

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
    const auto displayRange = readSpecDisplayRange(processor);
    const auto slope = displayRange.slope;
    const auto lowRange = displayRange.low;
    const auto highRange = displayRange.high;
    const auto binFrequency = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const auto highQuality = readParameterValue(
        processor, PluginProcessor::specHighQualityRenderingParameterId, 1.0f) >= 0.5f;
    const auto width = spectrogramImage.getWidth();
    const auto height = spectrogramImage.getHeight();
    const auto leftToRight = readParameterValue(
        processor, PluginProcessor::specMapLeftToRightParameterId, 0.0f) >= 0.5f;
    const auto directionState = leftToRight ? 1 : 0;
    if (renderedMapLeftToRight != directionState)
    {
        clearSpectrogram();
        renderedMapLeftToRight = directionState;
    }

    auto targetX = width - 1;
    if (leftToRight)
    {
        if (rtMapWriteColumn >= width)
            clearSpectrogram();
        targetX = juce::jlimit(0, width - 1, rtMapWriteColumn);
    }
    else if (width > 1)
    {
        spectrogramImage.moveImageSection(0, 0, 1, 0, width - 1, height);
        for (int y = 0; y < height; ++y)
        {
            auto* row = rtSpectrogramLevels.data() + static_cast<size_t>(y * width);
            std::move(row + 1, row + width, row);
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
        const auto lowerBandFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, lowerNormalised);
        const auto upperBandFrequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, upperNormalised);
        const auto frequency = ana::analyzer_frequency::frequencyAt(
            frequencyScale, lowFrequency, highFrequency, normalisedY);

        auto rawValue = sampleMapSpectrum(
            secondHalf ? secondSpectrum : firstSpectrum, binFrequency,
            lowerBandFrequency, frequency, upperBandFrequency, highQuality);
        if (! useSplitView && ! secondSpectrum.empty())
            rawValue = std::max(rawValue, sampleMapSpectrum(
                secondSpectrum, binFrequency, lowerBandFrequency, frequency,
                upperBandFrequency, highQuality));

        rtSpectrogramLevels[static_cast<size_t>(y * width + targetX)] = rawValue;
        const auto slopeOffset = frequency > 0.0f
            ? slope * std::log2(frequency / specSlopeReferenceFrequency) : 0.0f;
        const auto displayValue = rawValue + slopeOffset;
        const auto level = juce::jlimit(0.0f, 1.0f,
                                  (displayValue - lowRange) / (highRange - lowRange));
        pixels.setPixelColour(targetX, y, spectrogramColour(level));
    }

    if (leftToRight)
        ++rtMapWriteColumn;

    renderedMapRangeLow = lowRange;
    renderedMapRangeHigh = highRange;
    renderedMapSlope = slope;
    renderedMapHighQuality = highQuality ? 1 : 0;
    renderedMapFrequencyScale = static_cast<int>(frequencyScale);
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
    updateCursorReadouts();

    const juce::ScopedValueSetter<bool> guard(synchronisingRanges, true);
    frequencyRangeSlider.setRange(
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, lowFrequency),
        ana::analyzer_frequency::normalisedForFrequency(
            frequencyScale, ana::analyzer_frequency::minimumHz,
            ana::analyzer_frequency::maximumHz, highFrequency));
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
