#pragma once

#include "Controls.h"
#include "ParameterControl.h"
#include "SettingsSections.h"
#include "SettingsViewport.h"
#include "SlotsPanel.h"
#include "shell/OscPanel.h"
#include "shared/shell/AnalyzerPage.h"
#include "shared/scop/Crossover.h"

#include <cstddef>
#include <JuceHeader.h>

#include <functional>

class PluginProcessor;
class AnimationSettingsPanel;

class SettingsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SettingsPanel(PluginProcessor& processorRef);
    ~SettingsPanel() override;

    bool setAnalyzerContext(ana::AnalyzerPage page, const juce::String& viewMode);
    PluginProcessor& getProcessor() const { return processor; }
    void refreshExternalState();
    void setTrackInfo(int trackNumber, const juce::String& trackName);
    void attachMainControl(ControlButton& control, int index, bool allowDock = true);
    // SNAPSHOTS uses the existing snapshot editor, embedded in UNIT instead of a separate window.
    void attachAnimationContent(AnimationSettingsPanel& content);
    void detachAnimationContent();
    void openAnimationBlock();
    void attachSnapshotsContent(juce::Component& content, int preferredHeight,
                                const juce::Array<juce::Component*>& dockControls);
    void detachSnapshotsContent();
    void setSnapshotsPreferredHeight(int preferredHeight);
    juce::Array<juce::Component*> dockSources() const;
    void setActionStates(bool frozen, bool fullSource, bool realtime, bool fullSourceEnabled);
    void dismissParameterEditors();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void(bool)> onFreezeChanged;
    std::function<void()> onOverallFull;
    std::function<void()> onScopClear;
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onScopBandLayoutChanged;
    std::function<void(bool)> onScopLinkedNormalizeChanged;
    std::function<void(int)> onDisplaySelected;
    std::function<void()> onAnalysisModeChanged;
    std::function<void()> onScopSlopeChanged;
    std::function<void()> onLvlsPartsLayoutChanged;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void(ParameterControl&)> onResetRequested;
    std::function<void(bool)> onTextEditingChanged;

private:
    void configureControl(juce::Component* child);
    std::array<juce::Component::SafePointer<ControlButton>,4> mainActions;
    void timerCallback() override;
    void changeCrossoverCount(int delta);
    void constrainFrequency(size_t crossoverIndex);
    void refreshVisibilityLayout();
    juce::Identifier moduleCollapseKey() const;

    PluginProcessor& processor;
    EllipsisLabel trackInfo;
    ControlButton freezeControl { "FREEZE" };
    juce::Slider overallFullSlider, scopClearSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> overallFullAttachment,scopClearAttachment;
    NumericValueBox overallFullControl { overallFullSlider };
    NumericValueBox scopClearControl { scopClearSlider };
    ParameterControl compensationControl;
    ParameterControl modeControl;
    ParameterControl displayControl;
    ParameterControl screensaverControl;
    ParameterControl dockCountControl;
    AnimationSettingsPanel* animationContent=nullptr;
    OscPanel oscPanel;
    SlotsPanel slotsPanel;
    juce::Component* snapshotsContent = nullptr; // Non-owning: PluginEditor owns the snapshot editor.
    juce::Array<juce::Component*> snapshotDockControls;
    int snapshotsPreferredHeight = 160;
    bool syncingDisplayChoice = false;
    int pendingDisplayChoice = -1;
    ScopSettingsSection scopSettings;
    SpecSettingsSection specSettings;
    SpecSettingsSection specMapSettings;
    CorrSettingsSection corrSettings;
    CorrSettingsSection corrSignedSettings;
    CorrSettingsSection corrFreqSettings;
    LvlsSettingsSection lvlsSettings;
    class BlockContent final : public juce::Component
    {
    public:
        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        std::function<void(int)> onToggle;
        juce::Rectangle<int> unitHeader, moduleHeader, unitFrame, moduleFrame, mainHeader, mainFrame, animationHeader, animationFrame, snapshotsHeader, snapshotsFrame, oscHeader, oscFrame, slotsHeader, slotsFrame;
        std::array<juce::Rectangle<int>,7> innerFrames, innerHeaders;
        int hovered=0, pressed=0, hoverGeneration=0;
        bool hoverReady=false;
        juce::Component::SafePointer<ControlButton> disabledMarkTarget;
    } settingsContent;
    SettingsViewport settingsViewport;
    ana::AnalyzerPage analyzerPage = ana::AnalyzerPage::spec;
    juce::String analyzerViewMode { "FREQ" };
    bool constrainingFrequency = false;
    juce::String collapseSignature;
    std::vector<bool> layoutVisibility;
};
