#pragma once

#include "shared/shell/Controls.h"

#include <functional>

class PluginProcessor;

class OscPanel final : public juce::Component, private juce::Timer
{
public:
    explicit OscPanel(PluginProcessor&);
    ~OscPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refresh();
    void dismissEditor();
    void setListVisible(bool);

    std::function<void()> onListRequested;
    std::function<void()> onCloseRequested;
    std::function<void(bool)> onTextEditingChanged;

private:
    void beginEdit(ControlButton&, const juce::String&,
                   std::function<bool(const juce::String&)>);
    void finishEdit(bool shouldCommit);
    void timerCallback() override;

    PluginProcessor& processor;
    ControlButton enableButton { "OSC-ENABLE" };
    ControlButton listButton { "LIST" };
    ControlButton cleanViewButton { "CLEAN-VIEW" };
    ControlButton closeButton { "x" };
    juce::Label statusLabel;
    juce::Label inputPortLabel;
    juce::Label outputPortLabel;
    juce::Label outputHostLabel;
    ControlButton inputPortButton { "9000" };
    ControlButton outputPortButton { "9001" };
    ControlButton outputHostButton { "127.0.0.1" };
    juce::TextEditor valueEditor;
    ControlButton* editingButton = nullptr;
    std::function<bool(const juce::String&)> applyEditedValue;
    juce::Component* draggedWindow = nullptr;
    juce::Point<int> windowDragStartMouseScreen;
    juce::Point<int> windowDragStartTopLeft;
    bool draggingWindow = false;
};
