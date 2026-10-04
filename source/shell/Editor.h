#pragma once

#include "shared/spec/View.h"
#include "shared/corr/View.h"
#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "shared/shell/AboutPopup.h"
#include "shared/lvls/View.h"
#include "shared/scop/View.h"
#include "offline/shell/TrackLocationLabel.h"
#include "shared/shell/SettingsPanel.h"
#include "OscPanel.h"
#include "shared/shell/Page.h"

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

class PluginProcessor;
class SettingsWindow;
class SnapshotsWindow;
class OscWindow;
class OscListWindow;

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
    void showOscWindow(bool shouldShowWindow);
    void showOscListWindow(bool shouldShowWindow);
    void showAnalyzerPage(ana::AnalyzerPage page, bool shouldShowSettings);
    juce::String getActiveSettingsViewMode() const;
    void showChoicePrompt(juce::Rectangle<int> anchorBounds,
                          juce::StringArray choices,
                          int selectedIndex,
                          std::function<void(int)> onSelect,
                          std::vector<bool> enabledChoices = {});
    void dismissChoicePrompt();
    void showAboutPopup();
    void dismissAboutPopup();
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void showModePrompt();
    void showOfflineLocationPrompt();
    void showOfflineSourcePrompt();
    void showOfflineTakePrompt();
    void refreshOfflineSelectionButtons();
    void refreshOfflineUpdateStatus();

    PluginProcessor& audioProcessor;
    ScopView scopDisplay;
    SettingsPanel settingsComponent;
    SpecView specDisplay;
    CorrView corrDisplay;
    LvlsView lvlsDisplay;
    EllipsisLabel offlineUpdateLabel;
    EllipsisLabel offlineTrackNumberLabel;
    TrackLocationLabel offlineLocationLabel;
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<AboutPopup> aboutPopup;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<SnapshotsWindow> snapshotsWindow;
    std::unique_ptr<OscWindow> oscWindow;
    std::unique_ptr<OscListWindow> oscListWindow;
    std::unique_ptr<juce::ResizableEdgeComponent> rightEdgeResizer;
    std::unique_ptr<juce::ResizableEdgeComponent> bottomEdgeResizer;

    ControlButton specPageButton { "SPEC" };
    ControlButton modeButton { "MODE" };
    ControlButton sourceButton { "SOURCE" };
    ControlButton locationButton { "LOCATION" };
    ControlButton takeButton { "TAKE" };
    ControlButton keepSecondTakeButton { "KST" };
    ControlButton refreshButton { "refresh" };
    LongPressGesture refreshPress;
    std::vector<ana::offline::SourceChoice> offlineSourceChoices;
    bool displayedOffline = false;
    bool displayedModeAvailable = false;
    ControlButton corrPageButton { "CORR" };
    ControlButton lvlsPageButton { "LVLS" };
    ControlButton scopPageButton { "SCOP" };
    ControlButton snapshotsWindowButton { "hexagons" };
    ControlButton settingsButton { "adjustments-alt" };
    ControlButton fullSourceButton { "browser-maximize" };
    ControlButton clearButton { "eraser" };
    ControlButton freezeButton { "snowflake" };
    ControlButton oscWindowButton { "affiliate" };
    ControlButton aboutButton { "at" };
    juce::Point<int> pendingEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    ana::AnalyzerPage activePage = ana::AnalyzerPage::spec;
    bool showingAnalyzerSettings = false;
    bool cleanViewActive = false;
    bool showingSnapshotsWindow = false;
    bool showingOscWindow = false;
    bool showingOscListWindow = false;
    bool scopFrozen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
