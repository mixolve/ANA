#pragma once
#include "Settings.h"
#include <JuceHeader.h>
#include <optional>

struct AnimationField
{
    const char* label;
    const char* property;
    std::atomic<float> AnimationModeSettings::* value;
    double minimum, maximum, initial, interval;
    int decimals;
    bool burningFilm, topographic;
    bool supports(int mode) const noexcept { return mode == 1 ? burningFilm : mode == 2 && topographic; }
    double upperBound(int mode) const noexcept { return value == &AnimationModeSettings::speed && mode == 1 ? 8.0 : maximum; }
    double defaultValue(int mode) const noexcept { return value == &AnimationModeSettings::speed && mode == 1 ? 2.0 : initial; }
};
inline constexpr std::array<AnimationField, 5> animationFields {{
    { "SPEED", "speed", &AnimationModeSettings::speed, 0.0, 4.0, 1.0, 0.01, 2, true, true },
    { "GLOW", "glow", &AnimationModeSettings::glow, 0.0, 3.0, 1.0, 0.01, 2, true, false },
    { "SCALE", "scale-factor", &AnimationModeSettings::scaleFactor, 0.1, 2.0, 0.1, 0.01, 2, true, true },
    { "THICKNESS", "thickness", &AnimationModeSettings::thickness, 0.1, 4.0, 1.0, 0.01, 2, false, true },
    { "DENSITY", "density", &AnimationModeSettings::density, 2.0, 20.0, 14.0, 1.0, 0, false, true }
}};
inline constexpr AnimationField animationPauseDelayField {
    "DELAY", "animation-pause-delay", nullptr, 10.0, 60000.0, 1000.0, 1.0, 0, true, true
};
juce::String animationModeProperty(int mode, const char* property);
std::optional<bool> validateAnimationProperty(const juce::Identifier&, const juce::var&);
void loadAnimationSettings(AnimationSettings&, const juce::ValueTree&);

struct AnimationParameterDefinition
{
    juce::String property, title;
    double minimum, maximum, initial, interval;
    juce::StringArray choices;
    bool boolean=false;
    int mode=0;
    double propertyOffset=0;
};
const std::vector<AnimationParameterDefinition>& animationParameterDefinitions();
void addAnimationParameterLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout);
void syncAnimationPropertyToParameter(juce::AudioProcessorValueTreeState& parameters,const juce::Identifier& property);
void syncAnimationParametersToProperties(juce::AudioProcessorValueTreeState& parameters);
void restoreAnimationParameters(juce::AudioProcessorValueTreeState& parameters,const juce::ValueTree& saved);
