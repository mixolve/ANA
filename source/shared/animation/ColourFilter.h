#pragma once
#include "Settings.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>

// CSS-equivalent colour matrices for the ten artwork colours; alpha is preserved.
struct AnimationColourFilter
{
    std::array<std::array<float, 3>, 3> rows {{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }};

    explicit AnimationColourFilter(int index)
    {
        if (index == 8 || index == 9)
        {
            const auto brightness = index == 8 ? 1.1f : 1.0f;
            for (auto& row : rows)
                row = { 0.2126f * brightness, 0.7152f * brightness, 0.0722f * brightness };
            return;
        }
        constexpr std::array<float, 8> angles { 300, 0, 30, 90, 180, 175, 240, 300 };
        index = juce::jlimit(0, 7, index);
        const auto angle = angles[static_cast<size_t>(index)] * juce::MathConstants<float>::pi / 180.0f;
        const auto c = std::cos(angle), s = std::sin(angle);
        rows = {{
            { .213f + .787f*c - .213f*s, .715f - .715f*c - .715f*s, .072f - .072f*c + .928f*s },
            { .213f - .213f*c + .143f*s, .715f + .285f*c + .140f*s, .072f - .072f*c - .283f*s },
            { .213f - .213f*c - .787f*s, .715f - .715f*c + .715f*s, .072f + .928f*c + .072f*s }
        }};
        const auto saturation = index == 3 ? 1.2f : index == 4 ? .5f : (index == 0 || index == 7) ? 1.1f : 1.0f;
        const auto brightness = index == 4 ? 1.1f : 1.0f;
        const auto rotated = rows;
        for (size_t channel = 0; channel < 3; ++channel)
            for (size_t input = 0; input < 3; ++input)
                rows[channel][input] = brightness * (saturation * rotated[channel][input]
                    + (1 - saturation) * (.213f * rotated[0][input] + .715f * rotated[1][input]
                                          + .072f * rotated[2][input]));
    }

    juce::Colour apply(juce::Colour colour) const
    {
        const std::array<float, 3> rgb { colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue() };
        std::array<float, 3> out {};
        for (size_t channel = 0; channel < 3; ++channel)
            for (size_t input = 0; input < 3; ++input)
                out[channel] += rows[channel][input] * rgb[input];
        return juce::Colour::fromFloatRGBA(juce::jlimit(0.f, 1.f, out[0]), juce::jlimit(0.f, 1.f, out[1]),
                                          juce::jlimit(0.f, 1.f, out[2]), colour.getFloatAlpha());
    }
};
