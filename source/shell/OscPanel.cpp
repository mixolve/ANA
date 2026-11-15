#include "OscPanel.h"
#include "OscController.h"
#include "Processor.h"
#include "shared/shell/Theme.h"

#include <cmath>

namespace
{
bool parsePort(const juce::String& text, int& port)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || ! trimmed.containsOnly("0123456789"))
        return false;
    port = trimmed.getIntValue();
    return port >= 1 && port <= 65535;
}
}

OscPanel::OscPanel(PluginProcessor& owner) : processor(owner)
{
    getProperties().set("emptyEditorBackground",true);
    setOpaque(true);
    enableSlider.setRange(0,1,1);
    enableControl.setName("osc.enable");
    enableControl.setSquareStyle([] { return juce::String("OSC"); });
    enableControl.setDiscreteChoiceCount(2);
    enableControl.setSlotIndicatorVisible(false);
    enableControl.displayTextProvider=[this] { return enableSlider.getValue()>=0.5?juce::String("ON"):juce::String("OFF"); };
    enableControl.onChoiceClicked=[this](int) { enableSlider.setValue(1-enableSlider.getValue(),juce::sendNotificationSync); };
    enableSlider.onValueChange=[this] { auto settings=processor.getOscSettings(); settings.enabled=enableSlider.getValue()>=0.5; processor.setOscSettings(settings); refresh(); };
    addAndMakeVisible(enableControl);
    for(auto* button:{&inputPortButton,&outputPortButton,&outputHostButton}) { addAndMakeVisible(*button); button->setSquareStyle(true); button->setSlotIndicatorVisible(false); }
    inputPortButton.setButtonText("IN-PORT"); outputPortButton.setButtonText("OUT-PORT"); outputHostButton.setButtonText("HOST");
    inputPortButton.valueTextProvider=[this] { return juce::String(processor.getOscSettings().inputPort); };
    outputPortButton.valueTextProvider=[this] { return juce::String(processor.getOscSettings().outputPort); };
    outputHostButton.valueTextProvider=[this] { return processor.getOscSettings().outputHost; };

    const auto identify = [] (juce::Component& component, const char* id, const char* role)
    {
        component.setComponentID(id);
        component.getProperties().set("svgExportRole", role);
    };
    identify(inputPortButton, "osc-input-port-value", "value");
    identify(outputPortButton, "osc-output-port-value", "value");
    identify(outputHostButton, "osc-output-host-value", "value");
    identify(statusControl, "osc-status", "value");
    statusControl.setSquareStyle(true);
    statusControl.setSlotIndicatorVisible(false);
    statusControl.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(statusControl);

    inputPortButton.onClick = [this]
    {
        beginEdit(inputPortButton, juce::String(processor.getOscSettings().inputPort),
                  [this] (const juce::String& text)
                  {
                      int port = 0;
                      if (! parsePort(text, port))
                          return false;
                      auto settings = processor.getOscSettings();
                      settings.inputPort = port;
                      if (processor.setOscSettings(settings))
                          return true;
                      if (! settings.enabled)
                          return false;
                      settings.enabled = false;
                      return processor.setOscSettings(settings);
                  });
    };
    outputPortButton.onClick = [this]
    {
        beginEdit(outputPortButton, juce::String(processor.getOscSettings().outputPort),
                  [this] (const juce::String& text)
                  {
                      int port = 0;
                      if (! parsePort(text, port))
                          return false;
                      auto settings = processor.getOscSettings();
                      settings.outputPort = port;
                      return processor.setOscSettings(settings);
                  });
    };
    outputHostButton.onClick = [this]
    {
        beginEdit(outputHostButton, processor.getOscSettings().outputHost,
                  [this] (const juce::String& text)
                  {
                      const auto host = text.trim();
                      if (host.isEmpty() || host.length() > 253)
                          return false;
                      auto settings = processor.getOscSettings();
                      settings.outputHost = host;
                      return processor.setOscSettings(settings);
                  });
    };

    valueEditor.setFont(ana::ui::makeFont());
    valueEditor.setColour(juce::TextEditor::backgroundColourId, ana::ui::dark);
    valueEditor.setColour(juce::TextEditor::textColourId, ana::ui::white);
    valueEditor.setColour(juce::TextEditor::outlineColourId, ana::ui::light);
    valueEditor.setColour(juce::TextEditor::highlightColourId, juce::Colours::transparentBlack);
    valueEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setVisible(false);
    valueEditor.onReturnKey = [this] { finishEdit(true); };
    valueEditor.onEscapeKey = [this] { finishEdit(false); };
    addChildComponent(valueEditor);

    refresh();
    startTimerHz(4);
}

OscPanel::~OscPanel()
{
    stopTimer();
    dismissEditor();
}

void OscPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(ana::ui::windowBackground);
}

void OscPanel::resized()
{
    constexpr int side=80, step=88;
    enableControl.setBounds(0,0,side,side);
    inputPortButton.setBounds(step,0,side,side);
    outputPortButton.setBounds(2*step,0,side,side);
    statusControl.setBounds(3*step,0,side,side);
    outputHostButton.setBounds(4*step,0,2*side+8,side);
    if(editingButton) valueEditor.setBounds(editingButton->getBounds());
}

void OscPanel::beginEdit(ControlButton& button, const juce::String& initial,
                         std::function<bool(const juce::String&)> apply)
{
    if (editingButton != nullptr)
    {
        valueEditor.grabKeyboardFocus();
        return;
    }
    editingButton = &button;
    applyEditedValue = std::move(apply);
    valueEditor.setBounds(button.getBounds());
    valueEditor.setText(initial, false);
    valueEditor.setVisible(true);
    valueEditor.toFront(false);
    valueEditor.selectAll();
    if (onTextEditingChanged)
        onTextEditingChanged(true);
    valueEditor.enterModalState(true, nullptr, false);
}

void OscPanel::finishEdit(const bool shouldCommit)
{
    if (editingButton == nullptr)
        return;
    editingButton = nullptr;
    auto apply = std::move(applyEditedValue);
    const auto value = valueEditor.getText();
    valueEditor.exitModalState(0);
    valueEditor.setVisible(false);
    if (shouldCommit && apply)
        apply(value);
    if (onTextEditingChanged)
        onTextEditingChanged(false);
    refresh();
}

void OscPanel::dismissEditor()
{
    finishEdit(false);
}

void OscPanel::timerCallback()
{
    refresh();
}

void OscPanel::refresh()
{
    const auto settings = processor.getOscSettings();
    enableSlider.setValue(settings.enabled?1:0,juce::dontSendNotification);
    const auto text = processor.isOscInputPortBusy() ? juce::String("BUSY") : juce::String("FREE");
    if (statusControl.getButtonText() != text) statusControl.setButtonText(text);
}
