#pragma once

#include <JuceHeader.h>

namespace ana::osc
{
enum class ParameterSection { main, settings };

inline juce::String sectionTitle(const juce::String& group, const ParameterSection section)
{
    if (group == "GLOBAL")
        return group;
    return group + (section == ParameterSection::main ? " - MAIN" : " - SETTINGS");
}

// Classify by where a value is edited, not by the visibility of the control.
// Settings that show/hide controls belong to SETTINGS themselves.
inline ParameterSection parameterSection(const juce::String& id)
{
    const auto leaf = id.fromLastOccurrenceOf("/", false, false);
    if (id == "mode" || id == "module" || id == "clean-view" || id == "spec/view" || id == "corr/mode")
        return ParameterSection::main;
    if (id.startsWith("spec/freq/"))
    {
        if (leaf == "low" || leaf == "high" || leaf == "range-low" || leaf == "range-high"
            || leaf == "monitor-mode" || leaf == "split-view")
            return ParameterSection::main;
    }
    else if (id.startsWith("spec/map/"))
    {
        if (leaf == "low" || leaf == "high" || leaf == "monitor-mode"
            || leaf == "time-range-start" || leaf == "time-range-end")
            return ParameterSection::main;
    }
    else if (id.startsWith("corr/"))
    {
        if (leaf == "low" || leaf == "high" || leaf == "range-low" || leaf == "range-high")
            return ParameterSection::main;
    }
    else if (id.startsWith("lvls/peak-rms/"))
    {
        if (leaf == "mode" || leaf == "ms-mode")
            return ParameterSection::main;
    }
    else if (id.startsWith("lvls/history/"))
    {
        if (leaf == "solo" || leaf == "horizontal-start" || leaf == "horizontal-end"
            || leaf == "vertical-start" || leaf == "vertical-end")
            return ParameterSection::main;
    }
    else if (id.startsWith("scop/band-"))
    {
        if (leaf == "mode" || leaf == "zoom" || leaf == "normalize")
            return ParameterSection::main;
    }
    return ParameterSection::settings;
}
}
