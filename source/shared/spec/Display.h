#pragma once

#include "shell/Processor.h"
#include "shared/shell/AnalyzerViewUtilities.h"
#include "shared/shell/GraphColours.h"
#include "shared/spec/Settings.h"

namespace ana::spec::display
{
using ana::ui::analyzer_detail::readParameterValue;
inline constexpr float specSlopeReferenceFrequency = 632.0f;

struct SpecFrequencyRange
{
    float low = ana::frequency_scale::minimumHz;
    float high = ana::frequency_scale::maximumHz;
};

struct SpecDisplayRange
{
    float low = ana::spec::defaultDisplayLowDecibels;
    float high = ana::spec::defaultDisplayHighDecibels;
    float slope = ana::spec::defaultSlopeDecibelsPerOctave;
};

inline juce::Colour readGraphColour(const PluginProcessor& processor, const char* parameterId,
                             const int defaultIndex) noexcept
{
    return ana::ui::graphColour(juce::roundToInt(
        readParameterValue(processor, parameterId, static_cast<float>(defaultIndex))));
}

inline float readGraphOpacity(const PluginProcessor& processor) noexcept
{
    return juce::jlimit(0.01f, 1.0f, readParameterValue(
        processor, PluginProcessor::specGraphOpacityParameterId, 100.0f) * 0.01f);
}

inline SpecFrequencyRange readSpecFrequencyRange(const PluginProcessor& processor) noexcept
{
    const auto configuredLow = readParameterValue(
        processor, PluginProcessor::specLowParameterId,
        ana::frequency_scale::minimumHz);
    const auto configuredHigh = readParameterValue(
        processor, PluginProcessor::specHighParameterId,
        ana::frequency_scale::maximumHz);
    const auto low = juce::jlimit(
        ana::frequency_scale::minimumHz,
        ana::frequency_scale::maximumHz - ana::frequency_scale::minimumSpanHz,
        std::min(configuredLow, configuredHigh));
    return {
        low,
        juce::jlimit(low + ana::frequency_scale::minimumSpanHz, ana::frequency_scale::maximumHz,
                     std::max(configuredLow, configuredHigh))
    };
}

inline ana::frequency_scale::Scale readSpecFrequencyScale(const PluginProcessor& processor) noexcept
{
    return ana::frequency_scale::scaleFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::specFrequencyScaleParameterId,
        static_cast<float>(ana::frequency_scale::defaultScaleIndex))));
}

inline ana::spec::ColourMap readSpecColourMap(const PluginProcessor& processor) noexcept
{
    return ana::spec::colourMapFromIndex(juce::roundToInt(readParameterValue(
        processor, PluginProcessor::specMapColourMapParameterId,
        static_cast<float>(ana::spec::defaultColourMapIndex))));
}

inline SpecDisplayRange readSpecDisplayRange(const PluginProcessor& processor) noexcept
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

inline ana::spec::MonitorMode readSpecMonitorMode(const PluginProcessor& processor) noexcept
{
    const auto index = juce::jlimit(
        0, static_cast<int>(ana::spec::monitorModeCount) - 1,
        juce::roundToInt(readParameterValue(
            processor, PluginProcessor::specMonitorModeParameterId, 0.0f)));
    return static_cast<ana::spec::MonitorMode>(index);
}

inline ana::spec::SpecProcessor::DisplayType readSpecDisplayType(
    const PluginProcessor& processor, const char* parameterId) noexcept
{
    return readParameterValue(processor, parameterId, 0.0f) >= 0.5f
        ? ana::spec::SpecProcessor::DisplayType::maximum
        : ana::spec::SpecProcessor::DisplayType::average;
}

inline std::pair<ana::spec::Channel, ana::spec::Channel>
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

inline juce::Rectangle<float> splitPaneForCursor(const juce::Rectangle<float> plotBounds,
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

inline juce::String formatMapTime(const double seconds)
{
    const auto safeSeconds = std::max(0.0, seconds);
    const auto minutes = static_cast<int>(safeSeconds / 60.0);
    const auto secondsInMinute = safeSeconds - static_cast<double>(minutes) * 60.0;
    return juce::String::formatted("%02d:%06.3f", minutes, secondsInMinute);
}

inline juce::Colour spectrogramColour(const float normalisedLevel,
                               const ana::spec::ColourMap colourMap) noexcept
{
    const auto clampedLevel = juce::jlimit(0.0f, 1.0f, normalisedLevel);
    if (clampedLevel <= 0.0f)
        return ana::ui::background;
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

void drawSpec(const PluginProcessor& processor, juce::Graphics& graphics,
              const std::vector<float>& spec, int fftSize, double sampleRate,
              juce::Rectangle<float> plotBounds, juce::Colour lineColour,
              juce::Colour fillColour, bool allowFill = true, float gainDb = 0.0f);
}
