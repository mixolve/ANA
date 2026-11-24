#include "SubmoduleButtonAttachment.h"
#include "Controls.h"
#include "shell/Processor.h"

SubmoduleButtonAttachment::SubmoduleButtonAttachment(
    juce::AudioProcessorValueTreeState& stateIn, const juce::String& id, juce::Button& buttonIn, bool fixed)
    : state(stateIn), sourceId(id), button(buttonIn), fixedBinding(fixed)
{
    refreshBinding();
    button.addMouseListener(this, false);
    if (!fixedBinding) startTimerHz(10);
}

SubmoduleButtonAttachment::~SubmoduleButtonAttachment()
{
    stopTimer();
    button.removeMouseListener(this);
}

void SubmoduleButtonAttachment::refreshBinding()
{
    auto& processor = static_cast<PluginProcessor&>(state.processor);
    const auto id = fixedBinding ? sourceId : juce::String(processor.resolveParameterId(sourceId.toRawUTF8()));
    if (boundId == id)
        return;
    attachment.reset();
    boundId = id;
    button.getProperties().set("sourceParameterId", sourceId);
    button.getProperties().set("oscParameterId", id);
    if(auto* control=dynamic_cast<ControlButton*>(&button)) control->bindHostSlots(&processor.getHostSlots(),[safe=juce::Component::SafePointer<ControlButton>(control)] { return safe?safe->getProperties()["oscParameterId"].toString():juce::String(); });
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, button);
}
