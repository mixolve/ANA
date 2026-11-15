#include "State.h"
#include <cstdlib>
#include <cmath>

juce::String animationModeProperty(const int mode, const char* property)
{
    return juce::String("animation-") + animationModeName(mode) + "-" + property;
}
namespace
{
bool isInRange(const juce::var& value, double low, double high, bool integer = false)
{
    const auto text = value.toString().trim();
    if (text.isEmpty()) return false;
    const auto* begin = text.toRawUTF8();
    char* end = nullptr;
    const auto parsed = std::strtod(begin, &end);
    return end != begin && *end == '\0' && std::isfinite(parsed)
        && parsed >= low && parsed <= high && (!integer || std::floor(parsed) == parsed);
}
juce::var read(const juce::ValueTree& state, const juce::String& name, juce::var fallback)
{
    const auto value = state.getProperty(name, fallback);
    return validateAnimationProperty(juce::Identifier(name), value).value_or(false) ? value : fallback;
}
}
std::optional<bool> validateAnimationProperty(const juce::Identifier& property, const juce::var& value)
{
    if (property == juce::Identifier("animation-mode")) return isInRange(value, 1, 2, true);
    if (property == juce::Identifier("animation-play-on-pause")) return isInRange(value, 0, 1, true);
    if (property == juce::Identifier(animationPauseDelayField.property))
        return isInRange(value, animationPauseDelayField.minimum, animationPauseDelayField.maximum, true);
    for (int mode = 1; mode <= 2; ++mode)
    {
        if (property == juce::Identifier(animationModeProperty(mode, "color")))
            return isInRange(value, 0, static_cast<double>(animationColours.size() - 1), true);
        if (property == juce::Identifier(animationModeProperty(mode, "crop-y")))
            return isInRange(value, 0, 1);
        for (const auto& field : animationFields)
            if (field.supports(mode) && property == juce::Identifier(animationModeProperty(mode, field.property)))
                return isInRange(value, field.minimum, field.upperBound(mode), field.decimals == 0);
    }
    return std::nullopt;
}
void loadAnimationSettings(AnimationSettings& settings, const juce::ValueTree& state)
{
    settings.mode.store(1);
    settings.playOnPause.store(static_cast<bool>(read(state, "animation-play-on-pause", false)));
    settings.pauseDelayMs.store(static_cast<int>(read(state, animationPauseDelayField.property,
                                                     animationPauseDelayField.initial)));
    for (int modeId = 1; modeId <= 2; ++modeId)
    {
        auto& mode = settings.modes[static_cast<size_t>(modeId - 1)];
        mode.colour.store(static_cast<int>(read(state, animationModeProperty(modeId, "color"),
                                               static_cast<int>(animationColours.size() - 1))));
        mode.cropY.store(static_cast<float>(read(state, animationModeProperty(modeId, "crop-y"), 0.0f)));
        for (const auto& field : animationFields)
            if (field.supports(modeId))
                (mode.*field.value).store(static_cast<float>(read(state,
                    animationModeProperty(modeId, field.property), field.defaultValue(modeId))));
    }
}

const std::vector<AnimationParameterDefinition>& animationParameterDefinitions()
{
    static const auto definitions=[]
    {
        std::vector<AnimationParameterDefinition> result;
        result.push_back({"animation-play-on-pause","PLAY ON\nPAUSE",0,1,0,1,{},true});
        result.push_back({"animation-pause-delay","DELAY",10,60000,1000,1,{}});
        result.push_back({"animation-mode","MODE",0,1,0,1,{},false,0,1});
        juce::StringArray colours;
        for(auto colour:animationColours) colours.add(juce::String::toHexString(int(colour)).paddedLeft('0',6).toUpperCase());
        for(int mode=1;mode<=2;++mode)
        {
            result.push_back({animationModeProperty(mode,"color"),"COLOR",0,double(colours.size()-1),double(colours.size()-1),1,colours,false,mode});
            for(const auto& field:animationFields) if(field.supports(mode))
                result.push_back({animationModeProperty(mode,field.property),field.label,field.minimum,field.upperBound(mode),field.defaultValue(mode),field.interval,{},false,mode});
            result.push_back({animationModeProperty(mode,"crop-y"),"CROP Y",0,1,0,.01,{},false,mode});
        }
        return result;
    }();
    return definitions;
}
void addAnimationParameterLayout(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for(const auto& def:animationParameterDefinitions())
    {
        const juce::ParameterID id {def.property,1};
        const auto name="UNIT / ANIMATION / "+def.property.toUpperCase();
        if(def.property=="animation-mode") layout.add(std::make_unique<juce::AudioParameterInt>(id,"LEGACY ANIMATION MODE",0,1,0,juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));
        else if(def.boolean) layout.add(std::make_unique<juce::AudioParameterBool>(id,name,def.initial>=.5));
        else if(!def.choices.isEmpty()) layout.add(std::make_unique<juce::AudioParameterChoice>(id,name,def.choices,int(def.initial)));
        else if(def.interval>=1) layout.add(std::make_unique<juce::AudioParameterInt>(id,name,int(def.minimum),int(def.maximum),int(def.initial)));
        else layout.add(std::make_unique<juce::AudioParameterFloat>(id,name,
            juce::NormalisableRange<float>(float(def.minimum),float(def.maximum),float(def.interval)),float(def.initial)));
    }
}
void syncAnimationPropertyToParameter(juce::AudioProcessorValueTreeState& parameters,const juce::Identifier& property)
{
    for(const auto& def:animationParameterDefinitions()) if(property.toString()==def.property)
    {
        const auto value=parameters.state[property];
        if(!validateAnimationProperty(property,value).value_or(false)) return;
        auto* parameter=parameters.getParameter(def.property);
        const auto normalized=def.property=="animation-mode"?0.0f:parameter->convertTo0to1(float(value.toString().getDoubleValue()-def.propertyOffset));
        if(!juce::approximatelyEqual(parameter->getValue(),normalized)) parameter->setValueNotifyingHost(normalized);
        if(def.property=="animation-mode") parameters.state.setProperty(property,1,nullptr);
        return;
    }
}
void syncAnimationParametersToProperties(juce::AudioProcessorValueTreeState& parameters)
{
    for(const auto& def:animationParameterDefinitions())
    {
        auto* parameter=parameters.getParameter(def.property);
        if(def.property=="animation-mode" && parameter->getValue()>.0f) parameter->setValueNotifyingHost(0.0f);
        const double plain=parameter->convertFrom0to1(parameter->getValue())+def.propertyOffset;
        const juce::var value=def.interval>=1?juce::var(juce::roundToInt(plain)):juce::var(plain);
        parameters.state.setProperty(def.property,value,nullptr);
    }
}
void restoreAnimationParameters(juce::AudioProcessorValueTreeState& parameters,const juce::ValueTree& saved)
{
    for(const auto& def:animationParameterDefinitions())
        if(!saved.getChildWithProperty("id",def.property).isValid())
        {
            const auto property=juce::Identifier(def.property);
            const auto value=saved.getProperty(property,def.initial+def.propertyOffset);
            const auto plain=validateAnimationProperty(property,value).value_or(false)
                ?value.toString().getDoubleValue()-def.propertyOffset:def.initial;
            auto* parameter=parameters.getParameter(def.property);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(float(plain)));
        }
    syncAnimationParametersToProperties(parameters);
}
