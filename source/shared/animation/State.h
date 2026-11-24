#pragma once
#include "Settings.h"
#include <JuceHeader.h>
#include <vector>

struct AnimationField
{
    const char* label;
    const char* property;
    std::atomic<float> BurningFilmSettings::* value;
    double minimum, maximum, initial, interval;
};

inline constexpr std::array<AnimationField, 3> animationFields {{
    { "SPEED", "speed", &BurningFilmSettings::speed, 0.0, 8.0, 2.0, 0.01 },
    { "GLOW", "glow", &BurningFilmSettings::glow, 0.0, 3.0, 1.0, 0.01 },
    { "SCALE", "scale-factor", &BurningFilmSettings::scaleFactor, 0.1, 2.0, 0.1, 0.01 }
}};

inline constexpr AnimationField animationPauseDelayField {
    "DELAY", "animation-pause-delay", nullptr, 10.0, 60000.0, 1000.0, 1.0
};

juce::String burningFilmProperty(const char* property);
void loadAnimationSettings(AnimationSettings&, const juce::AudioProcessorValueTreeState&);

struct AnimationParameterDefinition
{
    juce::String property, title;
    double minimum, maximum, initial, interval;
    juce::StringArray choices;
    bool boolean = false;
};
const std::vector<AnimationParameterDefinition>& animationParameterDefinitions();
void addAnimationParameterLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout);
