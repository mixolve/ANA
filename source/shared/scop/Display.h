#pragma once

#include "Settings.h"
#include "ChannelData.h"
#include <JuceHeader.h>
#include <array>
#include <vector>

namespace ana::scop::display
{
inline constexpr std::array<const char*, scopChannelModeCount> scopModeButtonNames {
    "ST", "LR", "L", "R", "MS", "M", "S"
};
inline constexpr std::array<ScopChannelMode, scopChannelModeCount> scopModeButtonModes {
    ScopChannelMode::st, ScopChannelMode::lr, ScopChannelMode::left,
    ScopChannelMode::right, ScopChannelMode::ms, ScopChannelMode::mid,
    ScopChannelMode::side
};

struct DisplayedModes
{
    std::array<size_t, 2> indices {};
    size_t count = 1;
};

DisplayedModes getDisplayedModes(ScopChannelMode mode) noexcept;
juce::String formatZoomValue(float decibels);
void drawWaveformEnvelope(juce::Graphics& graphics,
                          const std::vector<float>& minimums,
                          const std::vector<float>& maximums,
                          juce::Rectangle<float> bounds, float rangeStart, float rangeEnd,
                          float verticalZoomDecibels, bool filledStyle);

// ST is a single envelope containing both channels. It preserves their peaks
// instead of adding samples (which would cause phase cancellation).
void drawStereoWaveformEnvelope(juce::Graphics& graphics,
                                const Envelope& left, const Envelope& right,
                                juce::Rectangle<float> bounds, float rangeStart, float rangeEnd,
                                float verticalZoomDecibels, bool filledStyle);
}
