#include "State.h"

juce::String burningFilmProperty(const char* property)
{
    return juce::String("animation-burning-film-") + property;
}

void loadAnimationSettings(AnimationSettings& settings,
                           const juce::AudioProcessorValueTreeState& parameters)
{
    const auto parameterValue = [&parameters] (const juce::String& id, const float fallback)
    {
        if (const auto* value = parameters.getRawParameterValue(id))
            return value->load(std::memory_order_relaxed);
        return fallback;
    };

    settings.playOnPause.store(parameterValue("animation-play-on-pause", 0.0f) >= 0.5f);
    settings.pauseDelayMs.store(juce::jlimit(
        static_cast<int>(animationPauseDelayField.minimum),
        static_cast<int>(animationPauseDelayField.maximum),
        juce::roundToInt(parameterValue(animationPauseDelayField.property,
                                        static_cast<float>(animationPauseDelayField.initial)))));

    auto& film = settings.burningFilm;
    film.colour.store(juce::jlimit(0, static_cast<int>(animationColours.size()) - 1,
        juce::roundToInt(parameterValue(burningFilmProperty("color"),
                                       static_cast<float>(animationColours.size() - 1)))));
    film.cropY.store(juce::jlimit(0.0f, 1.0f,
        parameterValue(burningFilmProperty("crop-y"), 0.0f)));
    for (const auto& field : animationFields)
        (film.*field.value).store(juce::jlimit(static_cast<float>(field.minimum),
            static_cast<float>(field.maximum),
            parameterValue(burningFilmProperty(field.property), static_cast<float>(field.initial))));
}

const std::vector<AnimationParameterDefinition>& animationParameterDefinitions()
{
    static const auto definitions = []
    {
        std::vector<AnimationParameterDefinition> result;
        result.push_back({ "animation-play-on-pause", "PLAY ON\nPAUSE", 0, 1, 0, 1, {}, true });
        result.push_back({ "animation-pause-delay", "DELAY", 10, 60000, 1000, 1, {} });
        juce::StringArray colours;
        for (auto colour : animationColours)
            colours.add(juce::String::toHexString(static_cast<int>(colour)).paddedLeft('0', 6).toUpperCase());
        result.push_back({ burningFilmProperty("color"), "COLOR", 0,
            static_cast<double>(colours.size() - 1), static_cast<double>(colours.size() - 1), 1, colours });
        for (const auto& field : animationFields)
            result.push_back({ burningFilmProperty(field.property), field.label,
                field.minimum, field.maximum, field.initial, field.interval, {} });
        result.push_back({ burningFilmProperty("crop-y"), "CROP Y", 0, 1, 0, 0.01, {} });
        return result;
    }();
    return definitions;
}

void addAnimationParameterLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (const auto& def : animationParameterDefinitions())
    {
        const juce::ParameterID id { def.property, 1 };
        const auto name = "UNIT / ANIMATION / " + def.property.toUpperCase();
        if (def.boolean)
            layout.add(std::make_unique<juce::AudioParameterBool>(id, name, def.initial >= 0.5));
        else if (! def.choices.isEmpty())
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, name, def.choices,
                                                                   static_cast<int>(def.initial)));
        else if (def.interval >= 1.0)
            layout.add(std::make_unique<juce::AudioParameterInt>(id, name,
                static_cast<int>(def.minimum), static_cast<int>(def.maximum), static_cast<int>(def.initial)));
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, name,
                juce::NormalisableRange<float>(static_cast<float>(def.minimum),
                    static_cast<float>(def.maximum), static_cast<float>(def.interval)),
                static_cast<float>(def.initial)));
    }
}
