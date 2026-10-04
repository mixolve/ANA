#pragma once

#include "FrequencyScale.h"

namespace ana::spectrogram_frequency
{
inline float frequencyAt(const float lowFrequency,
                         const float highFrequency,
                         const float normalised) noexcept
{
    return frequency_scale::frequencyAt(
        frequency_scale::Scale::logarithmic, lowFrequency, highFrequency, normalised);
}

inline float normalisedForFrequency(const float lowFrequency,
                                    const float highFrequency,
                                    const float frequency) noexcept
{
    return frequency_scale::normalisedForFrequency(
        frequency_scale::Scale::logarithmic, lowFrequency, highFrequency, frequency);
}
}
