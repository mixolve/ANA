#pragma once

#include "spec/View.h"
#include "corr/View.h"
#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "shared/shell/AboutPopup.h"
#include "shared/lvls/View.h"
#include "scop/View.h"
#include "shared/shell/SettingsPanel.h"
#include "shared/analyzer/Page.h"
#include "shell/AraSourceChoice.h"

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class PluginProcessor;
class SettingsWindow;
class SnapshotsWindow;

class PluginEditor final : public juce::AudioProcessorEditor
                                    , public juce::AudioProcessorEditorARAExtension
                                    , private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor& processorRef);
    ~PluginEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showAnalyzerSettings(bool shouldShowSettings);
    void showSnapshotsWindow(bool shouldShowWindow);
    void showAnalyzerPage(ana::AnalyzerPage page, bool shouldShowSettings);
    juce::String getActiveSettingsViewMode() const;
    void showChoicePrompt(juce::Rectangle<int> anchorBounds,
                          juce::StringArray choices,
                          int selectedIndex,
                          std::function<void(int)> onSelect,
                          std::vector<bool> enabledChoices = {});
    void showAraSourcePrompt();
    void showAraTakePrompt();
    void refreshAraSelectionButtons();
    void dismissChoicePrompt();
    void showAboutPopup();
    void dismissAboutPopup();
    void timerCallback() override;

    PluginProcessor& audioProcessor;
    ScopView scopDisplay;
    SettingsPanel settingsComponent;
    SpecView specDisplay;
    CorrView corrDisplay;
    LvlsView lvlsDisplay;
    EllipsisLabel araUpdateLabel;
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<AboutPopup> aboutPopup;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<SnapshotsWindow> snapshotsWindow;
    std::unique_ptr<juce::ResizableEdgeComponent> leftEdgeResizer;
    std::unique_ptr<juce::ResizableEdgeComponent> rightEdgeResizer;
    std::unique_ptr<juce::ResizableEdgeComponent> topEdgeResizer;
    std::unique_ptr<juce::ResizableEdgeComponent> bottomEdgeResizer;

    ControlButton specPageButton { "SPEC" };
    ControlButton corrPageButton { "CORR" };
    ControlButton lvlsPageButton { "LVLS" };
    ControlButton scopPageButton { "SCOP" };
    ControlButton snapshotsWindowButton { "hexagons" };
    ControlButton settingsButton { "adjustments-alt" };
    ControlButton sourceButton { "SOURCE" };
    ControlButton takeButton { "TAKE" };
    ControlButton refreshButton { "refresh" };
    ControlButton fullSourceButton { "browser-maximize" };
    ControlButton clearButton { "eraser" };
    ControlButton freezeButton { "snowflake" };
    ControlButton aboutButton { "I" };
    juce::Point<int> pendingEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    double araUpdateStatusMinimumEndMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    ana::AnalyzerPage activePage = ana::AnalyzerPage::spec;
    bool showingAnalyzerSettings = false;
    bool showingSnapshotsWindow = false;
    bool scopFrozen = false;
    std::vector<ana::ara::SourceChoice> araSourceChoices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
