#pragma once

#include "Controls.h"
#include "ParameterControl.h"
#include "SettingsSections.h"
#include "shared/analyzer/Page.h"
#include "shared/scop/Crossover.h"

#include <cstddef>
#include <JuceHeader.h>

#include <functional>

class PluginProcessor;

class SettingsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SettingsPanel(PluginProcessor& processorRef);
    ~SettingsPanel() override;

    void setAnalyzerContext(ana::AnalyzerPage page, const juce::String& viewMode);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onEqualBandHeights;
    std::function<void()> onCenterLvlsSections;
    std::function<void()> onCloseRequested;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void(ParameterControl&)> onResetRequested;
    std::function<void(bool)> onTextEditingChanged;

private:
    void timerCallback() override;
    void changeCrossoverCount(int delta);
    void constrainFrequency(size_t crossoverIndex);
    void refreshExternalState();
    void focusParameterControl(ParameterControl& control);
    void clearFocusedParameterControl();
    void dismissParameterEditors();
    void syncFocusedParameterControl();
    void updateGeneralHeading();

    PluginProcessor& processor;
    ScopSettingsSection scopSettings;
    SpecSettingsSection specSettings;
    CorrSettingsSection corrSettings;
    LvlsSettingsSection lvlsSettings;
    SliderLookAndFeel focusedControlLookAndFeel;
    FocusedPotentiometer focusedParameterControl;
    ControlButton closeButton { "xmark.circle" };
    ParameterControl* focusedParameterTarget = nullptr;
    juce::Component settingsContent;
    juce::Viewport settingsViewport;
    EllipsisLabel generalHeadingLabel;
    EllipsisLabel controlsVisibilityHeadingLabel;
    EllipsisLabel scopMainHeadingLabel;
    ana::AnalyzerPage analyzerPage = ana::AnalyzerPage::spec;
    juce::String analyzerViewMode { "FREQ" };
    juce::Component* draggedWindow = nullptr;
    juce::Point<int> windowDragStartMouseScreen;
    juce::Point<int> windowDragStartTopLeft;
    bool draggingWindow = false;
    bool constrainingFrequency = false;
    bool updatingFocusedParameterControl = false;
};
