#include "OscPanel.h"
#include "OscController.h"
#include "Processor.h"
#include "shared/shell/Theme.h"

#include <cmath>

namespace
{
void configureLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(ana::ui::makeFont());
    label.setColour(juce::Label::textColourId, ana::ui::white);
    label.setColour(juce::Label::backgroundColourId, ana::ui::dark);
    label.setColour(juce::Label::outlineColourId, ana::ui::light);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
}

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
    setOpaque(true);
    for (auto* button : { &enableButton, &listButton, &cleanViewButton,
                          &inputPortButton, &outputPortButton, &outputHostButton, &closeButton })
        addAndMakeVisible(*button);
    closeButton.setTooltip("CLOSE");
    closeButton.onClick = [this]
    {
        dismissEditor();
        if (onCloseRequested)
            onCloseRequested();
    };
    enableButton.setCondenseTextToFit(true);
    listButton.setClickingTogglesState(true);
    enableButton.onClick = [this]
    {
        auto settings = processor.getOscSettings();
        settings.enabled = ! settings.enabled;
        processor.setOscSettings(settings);
        refresh();
    };
    listButton.onClick = [this]
    {
        if (onListRequested)
            onListRequested();
    };
    cleanViewButton.onClick = [this]
    {
        if (auto* parameter = processor.getParameters().getParameter(PluginProcessor::cleanViewParameterId))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(processor.isCleanView() ? 0.0f : 1.0f);
            parameter->endChangeGesture();
        }
        refresh();
    };

    configureLabel(statusLabel, "FREE");
    configureLabel(inputPortLabel, "IN-PORT");
    configureLabel(outputPortLabel, "OUT-PORT");
    configureLabel(outputHostLabel, "HOST");
    for (auto* label : { &statusLabel, &inputPortLabel, &outputPortLabel, &outputHostLabel })
        addAndMakeVisible(*label);

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
    valueEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffbbbbbb));
    valueEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
    valueEditor.setJustification(juce::Justification::centred);
    valueEditor.setVisible(false);
    valueEditor.onReturnKey = [this] { finishEdit(true); };
    valueEditor.onEscapeKey = [this] { finishEdit(false); };
    valueEditor.onFocusLost = [this] { finishEdit(true); };
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
    graphics.fillAll(ana::ui::background);
    graphics.setColour(ana::ui::white);
    graphics.drawRect(getLocalBounds(), 1);
}

void OscPanel::resized()
{
    auto bounds = getLocalBounds().reduced(ana::ui::gap.pixels());
    auto footer = bounds.removeFromBottom(ana::ui::controlHeight);
    closeButton.setBounds(footer.removeFromRight(ana::ui::iconControlSize));
    ana::ui::gap.removeFromBottom(bounds);
    auto top = bounds.removeFromTop(ana::ui::controlHeight);
    enableButton.setBounds(top.removeFromLeft((top.getWidth() - ana::ui::gap.pixels() * 2) / 3));
    top.removeFromLeft(ana::ui::gap.pixels());
    listButton.setBounds(top.removeFromLeft((top.getWidth() - ana::ui::gap.pixels()) / 2));
    top.removeFromLeft(ana::ui::gap.pixels());
    statusLabel.setBounds(top);
    bounds.removeFromTop(ana::ui::gap.pixels());

    const auto placeField = [&bounds] (juce::Label& label, ControlButton& button)
    {
        auto row = bounds.removeFromTop(ana::ui::controlHeight);
        label.setBounds(row.removeFromLeft((row.getWidth() - ana::ui::gap.pixels()) / 2));
        row.removeFromLeft(ana::ui::gap.pixels());
        button.setBounds(row);
        bounds.removeFromTop(ana::ui::gap.pixels());
    };
    placeField(inputPortLabel, inputPortButton);
    placeField(outputPortLabel, outputPortButton);
    placeField(outputHostLabel, outputHostButton);
    cleanViewButton.setBounds(bounds.removeFromTop(ana::ui::controlHeight));
    if (editingButton != nullptr)
        valueEditor.setBounds(editingButton->getBounds());
}

void OscPanel::mouseDown(const juce::MouseEvent& event)
{
    if (event.originalComponent != this)
        return;

    draggedWindow = getTopLevelComponent();
    if (draggedWindow != nullptr && draggedWindow != this)
    {
        draggingWindow = true;
        windowDragStartMouseScreen = event.getScreenPosition();
        windowDragStartTopLeft = draggedWindow->getScreenBounds().getPosition();
    }
}

void OscPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (! draggingWindow || draggedWindow == nullptr)
        return;

    const auto delta = event.getScreenPosition() - windowDragStartMouseScreen;
    draggedWindow->setTopLeftPosition(windowDragStartTopLeft + delta);
}

void OscPanel::mouseUp(const juce::MouseEvent&)
{
    draggingWindow = false;
    draggedWindow = nullptr;
}

void OscPanel::setListVisible(const bool visible)
{
    listButton.setToggleState(visible, juce::dontSendNotification);
}

void OscPanel::beginEdit(ControlButton& button, const juce::String& initial,
                         std::function<bool(const juce::String&)> apply)
{
    dismissEditor();
    editingButton = &button;
    applyEditedValue = std::move(apply);
    valueEditor.setBounds(button.getBounds());
    valueEditor.setText(initial, false);
    valueEditor.setVisible(true);
    valueEditor.toFront(false);
    valueEditor.selectAll();
    if (onTextEditingChanged)
        onTextEditingChanged(true);
    valueEditor.grabKeyboardFocus();
}

void OscPanel::finishEdit(const bool shouldCommit)
{
    if (editingButton == nullptr)
        return;
    editingButton = nullptr;
    auto apply = std::move(applyEditedValue);
    const auto value = valueEditor.getText();
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
    cleanViewButton.setToggleState(processor.isCleanView(), juce::dontSendNotification);
    enableButton.setToggleState(settings.enabled, juce::dontSendNotification);
    enableButton.setButtonText(settings.enabled ? "OSC-ENABLED" : "OSC-ENABLE");
    statusLabel.setText(processor.isOscInputPortBusy() ? "BUSY" : "FREE",
                        juce::dontSendNotification);
    if (editingButton != &inputPortButton)
        inputPortButton.setButtonText(juce::String(settings.inputPort));
    if (editingButton != &outputPortButton)
        outputPortButton.setButtonText(juce::String(settings.outputPort));
    if (editingButton != &outputHostButton)
        outputHostButton.setButtonText(settings.outputHost);
}
