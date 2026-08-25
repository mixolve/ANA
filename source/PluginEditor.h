#pragma once

#include "MultibandScope.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class AnaAudioProcessor;

class AnaScopeButton final : public juce::Button
{
public:
    explicit AnaScopeButton(juce::String text);

    void paintButton(juce::Graphics& graphics,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;
};

class AnaSliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    int getSliderThumbRadius(juce::Slider&) override { return 0; }
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& graphics,
                          int x, int y, int width, int height,
                          float sliderPosition, float minimumSliderPosition, float maximumSliderPosition,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;
    void drawScrollbar(juce::Graphics& graphics,
                       juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
};

class AnaParameterControl final : public juce::Component, private juce::Timer
{
public:
    using Formatter = std::function<juce::String(double)>;

    AnaParameterControl(juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterId,
                        juce::String title,
                        Formatter formatter);
    ~AnaParameterControl() override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setInteractionEnabled(bool shouldEnable);
    bool isInteractionEnabled() const noexcept { return interactionEnabled; }
    bool isChoiceParameter() const noexcept { return choiceParameter != nullptr; }
    bool supportsFocusedPotentiometer() const noexcept
    {
        return choiceParameter == nullptr && boolParameter == nullptr;
    }
    juce::StringArray getChoiceNames() const;
    int getSelectedChoiceIndex() const noexcept;
    void setSelectedChoiceIndex(int choiceIndex);
    juce::Rectangle<int> getValueBounds() const noexcept { return valueBounds; }
    void commitPendingEditor();
    void setSelected(bool shouldSelect);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void(AnaParameterControl&)> onFocusRequested;
    std::function<void(AnaParameterControl&)> onChoiceRequested;
    std::function<void()> onValueChanged;

private:
    enum class PressRegion { none, title, value };

    void timerCallback() override;
    void showValueEditor();
    void hideValueEditor(bool discardChanges);
    void resetToDefault();

    juce::String titleText;
    Formatter valueFormatter;
    AnaSliderLookAndFeel sliderLookAndFeel;
    juce::Slider slider;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::AudioParameterChoice* choiceParameter = nullptr;
    juce::AudioParameterBool* boolParameter = nullptr;
    juce::TextEditor valueEditor;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> valueBounds;
    bool interactionEnabled = true;
    bool selected = false;
    bool compact = false;
    bool pointerDown = false;
    bool dragDetected = false;
    bool pressHighlighted = false;
    bool longPressArmed = false;
    bool valueEditorActive = false;
    PressRegion pressRegion = PressRegion::none;
};

class AnaChoicePrompt final : public juce::Component
{
public:
    AnaChoicePrompt(juce::Rectangle<int> anchorBounds,
                    juce::StringArray choices,
                    int selectedIndex,
                    std::function<void(int)> onSelect,
                    std::function<void()> onClose);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void choose(int index);
    void close();

    juce::Rectangle<int> anchorBounds;
    juce::StringArray choices;
    std::vector<std::unique_ptr<AnaScopeButton>> choiceButtons;
    juce::Rectangle<int> panelBounds;
    std::function<void(int)> onSelect;
    std::function<void()> onClose;
    bool closing = false;
};

class AnaLocalChoiceControl final : public juce::Component
{
public:
    explicit AnaLocalChoiceControl(juce::String title);

    void setChoices(juce::StringArray newChoices, int newSelectedIndex);
    const juce::StringArray& getChoiceNames() const noexcept { return choices; }
    int getSelectedChoiceIndex() const noexcept { return selectedIndex; }
    void setSelectedChoiceIndex(int newSelectedIndex, bool sendChange);
    juce::Rectangle<int> getValueBounds() const noexcept { return valueBounds; }
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void()> onChoiceRequested;
    std::function<void(int)> onSelectionChanged;

private:
    juce::String titleText;
    juce::StringArray choices;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> valueBounds;
    int selectedIndex = 0;
    bool pointerDown = false;
};

class AnaRangeSlider final : public juce::Component
{
public:
    float getRangeStart() const noexcept { return rangeStart; }
    float getRangeEnd() const noexcept { return rangeEnd; }

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::function<void()> onRangeChanged;

private:
    enum class DragMode { none, start, end, range };

    void updateRange(float newStart, float newEnd);

    float rangeStart = 0.0f;
    float rangeEnd = 1.0f;
    float dragStartRangeStart = 0.0f;
    float dragStartRangeEnd = 1.0f;
    DragMode dragMode = DragMode::none;
};

class AnaMultibandScopeComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaMultibandScopeComponent(AnaAudioProcessor& processorRef);

    void setFrozen(bool shouldFreeze);
    void clearHistory();
    void refreshWaveform();
    void equalizeBandHeights();
    void refreshDisplaySettings();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    std::function<void(const juce::String&)> onOfflineUpdateStatus;

private:
    void timerCallback() override;
    void resetHistory();
    void resizeHistory(size_t newColumnCount);
    void clearBandHistory(size_t bandIndex);
    void resetColumnAccumulator();
    void appendHistoryColumn(size_t activeBandCount);
    void renderOfflineSnapshot(std::shared_ptr<const ana::OfflineScopeSnapshot> snapshot);
    void refreshBandModeButtons();
    void normalizeBandWithZoom(size_t bandIndex);
    std::array<float, ana::OfflineScopeSnapshot::numChannelModes> getRealtimeModeSamples(
        size_t bandIndex, size_t sampleIndex) const noexcept;
    juce::Rectangle<float> getBandBounds(size_t bandIndex, size_t activeBandCount) const noexcept;
    int findBandSeparator(int y, size_t activeBandCount) const noexcept;
    size_t getWaveformColumnCount() const noexcept;

    AnaAudioProcessor& processor;
    ana::MultibandScope::Snapshot incomingSamples;
    AnaSliderLookAndFeel bandZoomLookAndFeel;
    std::array<std::array<std::unique_ptr<AnaScopeButton>, 6>, ana::MultibandScope::numBands> bandModeButtons;
    std::array<std::unique_ptr<AnaScopeButton>, ana::MultibandScope::numBands> bandClearButtons;
    std::array<std::unique_ptr<AnaScopeButton>, ana::MultibandScope::numBands> bandSingleViewButtons;
    std::array<std::unique_ptr<juce::Slider>, ana::MultibandScope::numBands> bandZoomSliders;
    std::array<std::unique_ptr<juce::Label>, ana::MultibandScope::numBands> bandZoomValueLabels;
    std::array<std::unique_ptr<AnaScopeButton>, ana::MultibandScope::numBands> bandNormalizeButtons;
    std::array<std::unique_ptr<AnaRangeSlider>, ana::MultibandScope::numBands> bandRangeSliders;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> historyChannelModes {};
    std::array<ana::OfflineScopeSnapshot::BandEnvelopes, ana::MultibandScope::numBands> historyEnvelopes;
    std::array<std::array<float, ana::OfflineScopeSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMinimums;
    std::array<std::array<float, ana::OfflineScopeSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMaximums;
    std::array<float, ana::MultibandScope::numBands> bandHeightWeights;
    std::array<bool, ana::MultibandScope::numBands> clearedBands {};
    std::shared_ptr<const ana::OfflineScopeSnapshot> displayedOfflineSnapshot;
    uint64_t readCursor = 0;
    double columnSampleProgress = 0.0;
    double historyTimeMilliseconds = 0.0;
    double realtimeResumeTimeMilliseconds = 0.0;
    size_t historyBandCount = 0;
    size_t offlineAnalysisColumnCount = 0;
    uint64_t offlineSnapshotRevision = 0;
    bool showingOfflineSnapshot = false;
    bool historyContainsRecordedData = false;
    bool frozen = false;
    bool updatingBandControls = false;
    int lastComponentWidth = 0;
    int lastComponentHeight = 0;
    int singleViewBand = -1;
    int draggedBandSeparator = -1;
};

class AnaCrossoverSettingsComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaCrossoverSettingsComponent(AnaAudioProcessor& processorRef);
    ~AnaCrossoverSettingsComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onEqualBandHeights;
    std::function<void(bool)> onOfflineSelectionChanged;
    std::function<void(AnaParameterControl&)> onChoiceRequested;

private:
    void timerCallback() override;
    void changeActiveSplitCount(int delta);
    void constrainFrequency(size_t crossoverIndex);
    void refreshExternalState();
    void focusParameterControl(AnaParameterControl& control);
    void clearFocusedParameterControl();
    void dismissParameterEditors();
    void syncFocusedParameterControl();

    AnaAudioProcessor& processor;
    AnaParameterControl styleControl;
    AnaParameterControl opacityControl;
    AnaParameterControl timeControl;
    AnaScopeButton addCrossoverButton { "XOV-ADD" };
    AnaScopeButton removeCrossoverButton { "XOV-DEL" };
    AnaScopeButton equalHeightButton { "EQUAL-HEIGHT" };
    AnaScopeButton zoomControlsButton { "ZOOM" };
    AnaScopeButton monitorControlsButton { "MONITOR" };
    AnaScopeButton otherControlsButton { "OTHERS" };
    AnaScopeButton alwaysSecondTakeButton { "ALWAYS-2ND-TAKE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zoomControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> otherControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alwaysSecondTakeAttachment;
    std::array<std::unique_ptr<AnaParameterControl>, ana::dsp::Crossover::numSplits> crossoverControls;
    AnaSliderLookAndFeel focusedControlLookAndFeel;
    juce::Slider focusedParameterControl;
    AnaParameterControl* focusedParameterTarget = nullptr;
    juce::Component settingsContent;
    juce::Viewport settingsViewport;
    juce::Label headingLabel;
    juce::Label generalHeadingLabel;
    juce::Label controlsVisibilityHeadingLabel;
    juce::Label realtimeHeadingLabel;
    juce::Label offlineHeadingLabel;
    bool constrainingFrequency = false;
    bool updatingFocusedParameterControl = false;
};

class AnaAudioProcessorEditor final : public juce::AudioProcessorEditor
#if JucePlugin_Enable_ARA
                                    , public juce::AudioProcessorEditorARAExtension
#endif
                                    , private juce::Timer
{
public:
    explicit AnaAudioProcessorEditor(AnaAudioProcessor& processorRef);
    ~AnaAudioProcessorEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showCrossoverSettings(bool shouldShowSettings);
    void showChoicePrompt(AnaParameterControl& control);
    void showChoicePrompt(juce::Rectangle<int> anchorBounds,
                          juce::StringArray choices,
                          int selectedIndex,
                          std::function<void(int)> onSelect);
    void showOfflineSourcePrompt();
    void showOfflineTakePrompt();
    void refreshOfflineSelectionButtons();
    void dismissChoicePrompt();
    void timerCallback() override;

    AnaAudioProcessor& audioProcessor;
    AnaMultibandScopeComponent scopeDisplay;
    AnaCrossoverSettingsComponent crossoverSettings;
    juce::Label offlineUpdateLabel;
    std::unique_ptr<AnaChoicePrompt> choicePrompt;

    AnaScopeButton frequencyButton { "FREQUENCY" };
    AnaScopeButton phaseButton { "PHASE" };
    AnaScopeButton scopeButton { "SCOPE" };
    AnaScopeButton settingsButton { "SETTINGS" };
    AnaScopeButton realtimeButton { "REALTIME" };
    AnaScopeButton offlineButton { "OFFLINE" };
    AnaScopeButton sourceButton { "SOURCE" };
    AnaScopeButton takeButton { "TAKE" };
    AnaScopeButton refreshButton { "REFRESH" };
    AnaScopeButton clearButton { "CLEAR" };
    AnaScopeButton freezeButton { "FREEZE" };
    juce::Point<int> pendingEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    bool showingCrossoverSettings = false;
    std::vector<ana::OfflineSourceTakeChoice> offlineSourceTakeChoices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaAudioProcessorEditor)
};
