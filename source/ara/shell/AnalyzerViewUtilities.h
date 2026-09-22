#pragma once

#include "shared/shell/Controls.h"
#include "Processor.h"
#include "shared/shell/Theme.h"
#include "shared/analyzer/Frequency.h"

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace ana::ui::analyzer_detail
{
inline constexpr int bandRangeSliderHeight = 14;
inline constexpr int bandZoomSliderWidth = bandRangeSliderHeight;

inline juce::String formatReadoutFrequency(const double frequency)
{
    return juce::String::formatted("%08.2f", std::max(0.0, frequency));
}

inline juce::String formatReadoutLevel(const double level)
{
    return juce::String::formatted("%+07.2f", level);
}

inline juce::String formatCorrCoefficient(const double coefficient)
{
    return juce::String::formatted("%+.2f", coefficient);
}

inline float frequencyToNormalised(const float frequency) noexcept
{
    return ana::analyzer_frequency::toLogNormalised(frequency);
}

inline float normalisedToFrequency(const float normalised) noexcept
{
    return ana::analyzer_frequency::fromLogNormalised(normalised);
}

inline float readParameterValue(const PluginProcessor& processor,
                                const char* parameterId,
                                const float fallback) noexcept
{
    if (const auto* value = processor.getParameters().getRawParameterValue(parameterId))
        return value->load(std::memory_order_relaxed);

    return fallback;
}

inline bool updateAraRevision(const PluginProcessor& processor,
                                  uint64_t& displayedRevision)
{
    const auto analysisResult = processor.getAraAnalysisResult();
    if (analysisResult == nullptr || displayedRevision == analysisResult->revision)
        return false;

    displayedRevision = analysisResult->revision;
    return true;
}

inline void configureCursorReadoutLabel(EllipsisLabel& label)
{
    label.setFont(ana::ui::makeFont());
    label.setJustificationType(juce::Justification::centred);
    label.setText("---", juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, ana::ui::white);
    label.setColour(juce::Label::backgroundColourId, ana::ui::dark);
    label.setColour(juce::Label::outlineColourId, ana::ui::light);
    label.setBorderSize(juce::BorderSize<int>(1, ana::ui::gap.pixels(), 1, ana::ui::gap.pixels()));
    label.setTextVerticalOffset(ana::ui::readoutTextVerticalOffset);
    label.setInterceptsMouseClicks(false, false);
}
}
