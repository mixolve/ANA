#pragma once

#include <JuceHeader.h>
#include "shared/spec/Settings.h"
#include <array>
#include <cstring>
#include <memory>

namespace ana::submodules
{
inline juce::String parameterName(const juce::String& id)
{
    return id.toUpperCase().replace("/", " / ");
}

inline constexpr std::array<std::array<const char*, 2>, 19> specIds {{
    { "spec/freq/fft-size", "spec/map/fft-size" },
    { "spec/freq/fft-overlap", "spec/map/fft-overlap" },
    { "spec/freq/freq-scale", "spec/map/freq-scale" },
    { "spec/freq/slope", "spec/map/slope" },
    { "spec/freq/low", "spec/map/low" },
    { "spec/freq/high", "spec/map/high" },
    { "spec/freq/range-low", "spec/map/range-low" },
    { "spec/freq/range-high", "spec/map/range-high" },
    { "spec/freq/ro-horiz", "spec/map/ro-horiz" },
    { "spec/freq/ro-vert", "spec/map/ro-vert" },
    { "spec/freq/clear-on-play", "spec/map/clear-on-play" },
    { "spec/freq/reset-click", "spec/map/reset-click" },
    { "spec/freq/cur-notes", "spec/map/cur-notes" },
    { "spec/freq/cur-horiz", "spec/map/cur-horiz" },
    { "spec/freq/cur-vert", "spec/map/cur-vert" },
    { "spec/freq/monitor", "spec/map/monitor" },
    { "spec/freq/zoom-horiz", "spec/map/zoom-horiz" },
    { "spec/freq/zoom-vert", "spec/map/zoom-vert" },
    { "spec/freq/monitor-mode", "spec/map/monitor-mode" }
}};

inline constexpr std::array<std::array<const char*, 3>, 25> corrIds {{
    { "corr/phase/fft-size", "corr/freq/fft-size", "corr/signed/fft-size" },
    { "corr/phase/fft-overlap", "corr/freq/fft-overlap", "corr/signed/fft-overlap" },
    { "corr/phase/avg-time", "corr/freq/avg-time", "corr/signed/avg-time" },
    { "corr/phase/smoothing", "corr/freq/smoothing", "corr/signed/smoothing" },
    { "corr/phase/freq-scale", "corr/freq/freq-scale", "corr/signed/freq-scale" },
    { "corr/phase/filled", "corr/freq/filled", "corr/signed/filled" },
    { "corr/phase/graph-2", "corr/freq/graph-2", "corr/signed/graph-2" },
    { "corr/phase/graph-1-type", "corr/freq/graph-1-type", "corr/signed/graph-1-type" },
    { "corr/phase/graph-2-type", "corr/freq/graph-2-type", "corr/signed/graph-2-type" },
    { "corr/phase/graph-1-color", "corr/freq/graph-1-color", "corr/signed/graph-1-color" },
    { "corr/phase/graph-2-color", "corr/freq/graph-2-color", "corr/signed/graph-2-color" },
    { "corr/phase/graph-opacity", "corr/freq/graph-opacity", "corr/signed/graph-opacity" },
    { "corr/phase/clear-on-play", "corr/freq/clear-on-play", "corr/signed/clear-on-play" },
    { "corr/phase/reset-click", "corr/freq/reset-click", "corr/signed/reset-click" },
    { "corr/phase/ro-horiz", "corr/freq/ro-horiz", "corr/signed/ro-horiz" },
    { "corr/phase/ro-vert", "corr/freq/ro-vert", "corr/signed/ro-vert" },
    { "corr/phase/cur-notes", "corr/freq/cur-notes", "corr/signed/cur-notes" },
    { "corr/phase/cur-horiz", "corr/freq/cur-horiz", "corr/signed/cur-horiz" },
    { "corr/phase/cur-vert", "corr/freq/cur-vert", "corr/signed/cur-vert" },
    { "corr/phase/zoom-horiz", "corr/freq/zoom-horiz", "corr/signed/zoom-horiz" },
    { "corr/phase/zoom-vert", "corr/freq/zoom-vert", "corr/signed/zoom-vert" },
    { "corr/phase/low", "corr/freq/low", "corr/signed/low" },
    { "corr/phase/high", "corr/freq/high", "corr/signed/high" },
    { "corr/phase/range-low", "corr/freq/range-low", "corr/signed/range-low" },
    { "corr/phase/range-high", "corr/freq/range-high", "corr/signed/range-high" }
}};

// UI controls use the parameter belonging to their selected submodule.
// Every returned ID names a separate APVTS parameter, also addressed directly by OSC.
inline const char* selectedId(const char* id, const bool specMap, const int corrMode) noexcept
{
    if (specMap)
        for (const auto& ids : specIds)
            if (std::strcmp(id, ids[0]) == 0)
                return ids[1];
    if (corrMode != 0)
        for (const auto& ids : corrIds)
            if (std::strcmp(id, ids[0]) == 0)
                return ids[static_cast<size_t>(juce::jlimit(0, 2, corrMode))];
    return id;
}

class ParameterLayout
{
public:
    template <typename Parameter>
    void add(std::unique_ptr<Parameter> parameter)
    {
        const auto id = parameter->paramID;
        for (const auto& ids : specIds)
            if (id == ids[0])
                layout.add(clone(*parameter, ids[1]));
        for (const auto& ids : corrIds)
            if (id == ids[0])
                for (size_t mode = 1; mode < ids.size(); ++mode)
                    layout.add(clone(*parameter, ids[mode]));
        layout.add(std::move(parameter));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout release() { return std::move(layout); }

private:
    static std::unique_ptr<juce::RangedAudioParameter> clone(
        juce::RangedAudioParameter& source, const juce::String& id)
    {
        const juce::ParameterID parameterId { id, 1 };
        const auto name = parameterName(id);
        const auto& range = source.getNormalisableRange();
        const auto defaultValue = source.convertFrom0to1(source.getDefaultValue());
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&source))
            return std::make_unique<juce::AudioParameterChoice>(
                parameterId, name, choice->choices, juce::roundToInt(defaultValue),
                juce::AudioParameterChoiceAttributes()
                    .withAutomatable(source.isAutomatable()).withMeta(source.isMetaParameter()));
        if (dynamic_cast<juce::AudioParameterBool*>(&source) != nullptr)
            return std::make_unique<juce::AudioParameterBool>(
                parameterId, name, defaultValue >= 0.5f,
                juce::AudioParameterBoolAttributes()
                    .withAutomatable(source.isAutomatable()).withMeta(source.isMetaParameter()));
        if (dynamic_cast<juce::AudioParameterInt*>(&source) != nullptr)
            return std::make_unique<juce::AudioParameterInt>(
                parameterId, name, juce::roundToInt(range.start), juce::roundToInt(range.end),
                juce::roundToInt(defaultValue), juce::AudioParameterIntAttributes()
                    .withAutomatable(source.isAutomatable()).withMeta(source.isMetaParameter()));
        auto clonedRange = range;
        auto clonedDefault = defaultValue;
        if (id.startsWith("spec/map/range-"))
            clonedRange.end = ana::spec::maximumMapDisplayDecibels
                - (id.endsWith("low") ? ana::spec::minimumDisplaySpanDecibels : 0.0f);
        if (id.startsWith("corr/freq/range-"))
        {
            clonedRange.start = 0.0f;
            clonedDefault = id.endsWith("low") ? 0.0f : 1.0f;
        }
        return std::make_unique<juce::AudioParameterFloat>(
            parameterId, name, clonedRange, clonedDefault, juce::AudioParameterFloatAttributes()
                .withAutomatable(source.isAutomatable()).withMeta(source.isMetaParameter())
                .withStringFromValueFunction([sourcePtr = &source] (const float value, const int length)
                {
                    return sourcePtr->getText(sourcePtr->convertTo0to1(value), length);
                })
                .withValueFromStringFunction([sourcePtr = &source] (const juce::String& text)
                {
                    return sourcePtr->convertFrom0to1(sourcePtr->getValueForText(text));
                }));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
};
}
