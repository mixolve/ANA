#pragma once

#include "shared/spec/View.h"
#include "shared/shell/SvgExport.h"
#include "shared/shell/ViewportOverflow.h"
#include "shared/corr/View.h"
#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"
#include "shared/shell/AboutPopup.h"
#include "shared/lvls/View.h"
#include "shared/scop/View.h"
#include "offline/shell/TrackLocationLabel.h"
#include "shared/shell/SettingsPanel.h"
#include "shared/animation/View.h"
#include "shared/shell/AnalyzerPage.h"
#include "shared/shell/WindowDragHandle.h"

#include <JuceHeader.h>

#include <array>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>

class PluginProcessor;
class SettingsWindow;
class SnapshotsWindowContent;

class PluginEditor final : public juce::AudioProcessorEditor
                                    , public juce::AudioProcessorEditorARAExtension
                                    , public ana::svg_export::Layout
                                    , private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor& processorRef);
    ~PluginEditor() override;

    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    int getSvgExportCanvasHeight() const override
    { return getHeight() + gui_export_layout::viewportOverflow(*this); }
    void prepareSvgExportLayout(int height) override
    { svgLayoutBounds = getLocalBounds().withHeight(height); svgLayoutActive = true; resized(); }
    void finishSvgExportLayout() override
    { svgLayoutBounds = getLocalBounds(); resized(); svgLayoutActive = false; }


private:
    bool squareResizeMode = false;
    void toggleSquareResizeMode();
    int requiredHeightForCurrentPage() const noexcept;
    int minimumHeightForCurrentPage() const noexcept;
    void updateResizeLimits();
    void restoreProcessorState();
    uint64_t displayedStateRevision = 0;
    bool restoringEditorState = false;
    // Retain the current height on an Offline -> SCOP page switch. Apply the
    // SCOP minimum only after the host/user actually changes the height.
    bool deferOfflineScopMinimum = false;
    int heightAtScopSwitch = 0;
    bool svgLayoutActive = false;
    juce::Rectangle<int> svgLayoutBounds;
    static bool isSvgExportGesture(const juce::ModifierKeys& modifiers);
    void showAnalyzerSettings(bool shouldShowSettings);
    void registerDockScreenset();
    void unregisterDockScreenset();
    static std::intptr_t dockScreensetCallback(int action, const char* id,
                                               void* context, void* data, int size);
    std::intptr_t handleDockScreenset(int action, void* data, int size);
    void loadDockScreensetState(juce::String serialized);
    bool dockScreensetInitializationComplete = false;
    bool dockScreensetRegistered = false;
    std::string dockScreensetId;
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
    void showAnimation(bool active);
    void ensureAnimationComponents();
    void showAnimationSettings();
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void showOfflineTakePrompt();
    void refreshOfflineSelectionButtons();
    void refreshOfflineActionControls();
    void refreshOfflineUpdateStatus();
    void syncDisplayedEditorSize();

#if JUCE_MAC
    void* svgExportKeyboardMonitor = nullptr;
#endif
    PluginProcessor& audioProcessor;
    ScopView scopDisplay;
    SettingsPanel settingsComponent;
    SpecView specDisplay;
    CorrView corrDisplay;
    LvlsView lvlsDisplay;
    ControlButton updateButton { "UD" };
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<AboutPopup> aboutPopup;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<SnapshotsWindowContent> snapshotsContent;
    std::shared_ptr<AnimationSettings> animationSettings = std::make_shared<AnimationSettings>();
    std::unique_ptr<AnimationBackdrop> animationBackdrop;
    std::unique_ptr<AnimationOverlay> animationOverlay;
    std::unique_ptr<AnimationSettingsPanel> animationSettingsPanel;
    std::unique_ptr<juce::ComponentBoundsConstrainer> editorSizeConstrainer;
    std::unique_ptr<juce::Component> cornerResizer;
    std::unique_ptr<juce::Component> leftEdgeResizer;
    std::unique_ptr<juce::Component> topEdgeResizer;
    std::unique_ptr<juce::Component> rightEdgeResizer;
    std::unique_ptr<juce::Component> bottomEdgeResizer;

    ControlButton locationButton { "MONITOR" };
    ControlButton takeButton { "TAKE" };
    ControlButton keepSecondTakeButton { "KEEP\nSECOND" };
    ControlButton refreshButton { "REFRESH" };
    std::vector<ana::offline::SourceChoice> offlineSourceChoices;
    bool displayedOffline = false;
    bool displayedModeAvailable = false;
    bool offlineActionControlsInitialized = false;
    ControlButton fullSourceButton { "OF" };
    ControlButton clearButton { "clear" };
    ControlButton freezeButton { "FRZ" };
    ControlButton aboutButton { "ABOUT" };
    WindowDragHandle windowDragHandle { *this };
    bool udDragPressed=false, udDragMoved=false;
    juce::Point<int> udDragStart;
    juce::Point<int> pendingEditorSize;
    juce::Point<int> lastDisplayedEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    ana::AnalyzerPage activePage = ana::AnalyzerPage::spec;
    bool showingAnalyzerSettings = false;
    bool cleanViewActive = false;
    bool scopFrozen = false;
    double animationPausedSince = -1.0;
    bool automaticPauseAnimation = false;
    bool pauseAnimationDismissed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
