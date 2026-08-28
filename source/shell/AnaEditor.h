#pragma once

#include "../scope/MultibandWaveformProcessor.h"

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
    void setCompact(bool shouldUseCompactLayout);
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
    enum class Orientation { horizontal, vertical };

    explicit AnaRangeSlider(Orientation newOrientation = Orientation::horizontal)
        : orientation(newOrientation) {}

    float getRangeStart() const noexcept { return rangeStart; }
    float getRangeEnd() const noexcept { return rangeEnd; }
    void setRange(float newStart, float newEnd);

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
    Orientation orientation;
};

class AnaFrequencyDisplayComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaFrequencyDisplayComponent(AnaAudioProcessor& processorRef);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void syncRangeSliders();
    void updateFrequencyRangeFromSlider();
    void updateMagnitudeRangeFromSlider();
    void refreshMonitorControls();
    juce::Rectangle<float> getPlotBounds() const noexcept;
    void drawSpectrum(juce::Graphics& graphics, const std::vector<float>& spectrum,
                      int fftSize, double sampleRate, juce::Rectangle<float> plotBounds,
                      juce::Colour colour) const;
    static float frequencyToNormalised(float frequency) noexcept;
    static float normalisedToFrequency(float normalised) noexcept;

    AnaAudioProcessor& processor;
    AnaParameterControl frequencyLowControl;
    AnaParameterControl frequencyHighControl;
    AnaParameterControl rangeLowControl;
    AnaParameterControl rangeHighControl;
    juce::Label cursorReadoutLabel;
    juce::Label cursorNoteReadoutLabel;
    juce::Label cursorVerticalReadoutLabel;
    std::array<std::unique_ptr<AnaScopeButton>, 7> monitorButtons;
    AnaScopeButton splitButton { "SPLIT" };
    AnaRangeSlider frequencyRangeSlider;
    AnaRangeSlider magnitudeRangeSlider { AnaRangeSlider::Orientation::vertical };
    std::vector<float> primarySpectrum;
    std::vector<float> secondarySpectrum;
    uint64_t displayedRevision = 0;
    uint64_t displayedOfflineRevision = 0;
    bool synchronisingRanges = false;
    juce::Point<float> cursorPosition;
    bool cursorInside = false;
    float lastCursorFrequency = 0.0f;
};

class AnaCorrelationDisplayComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaCorrelationDisplayComponent(AnaAudioProcessor& processorRef);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void(const juce::String&)> onOfflineUpdateStatus;

private:
    void timerCallback() override;
    void syncRangeSliders();
    void updateFrequencyRangeFromSlider();
    void updateCorrelationRangeFromSlider();
    void refreshControls();
    void setCorrelationMode(int mode);
    void drawCorrelation(juce::Graphics& graphics, const std::vector<float>& values,
                         const juce::Rectangle<float> plotBounds, float lowFrequency,
                         float highFrequency, float lowRange, float highRange,
                         double sampleRate, int fftSize, juce::Colour colour);
    juce::Rectangle<float> getPlotBounds() const noexcept;
    static float frequencyToNormalised(float frequency) noexcept;
    static float normalisedToFrequency(float normalised) noexcept;

    AnaAudioProcessor& processor;
    AnaParameterControl frequencyLowControl;
    AnaParameterControl frequencyHighControl;
    AnaParameterControl rangeLowControl;
    AnaParameterControl rangeHighControl;
    juce::Label cursorReadoutLabel;
    juce::Label cursorVerticalReadoutLabel;
    AnaScopeButton phaseModeButton { "PHASE" };
    AnaScopeButton amplitudeModeButton { "AMPLITUDE" };
    AnaRangeSlider frequencyRangeSlider;
    AnaRangeSlider correlationRangeSlider { AnaRangeSlider::Orientation::vertical };
    std::vector<float> primaryCorrelation;
    std::vector<float> secondaryCorrelation;
    std::vector<float> correlationColumns;
    std::vector<int> correlationColumnCounts;
    uint64_t displayedRevision = 0;
    uint64_t displayedOfflineRevision = 0;
    bool synchronisingRanges = false;
    juce::Point<float> cursorPosition;
    bool cursorInside = false;
    float lastCursorFrequency = 0.0f;
    bool offlineRenderPending = false;
};

enum class AnaAnalyzerPage { scope, frequency, correlation };

class AnaMultibandScopeComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaMultibandScopeComponent(AnaAudioProcessor& processorRef);

    void setFrozen(bool shouldFreeze);
    void clearHistory();
    void refreshWaveform();
    void equalizeBandHeights();
    void refreshDisplaySettings();
    void setFullSourceView(bool shouldShowFullSource);
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
    std::array<float, ana::OfflineScopeSnapshot::numChannelModes> getRealtimeWidebandModeSamples(
        size_t sampleIndex) const noexcept;
    juce::Rectangle<float> getBandBounds(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool shouldShowZoomSliders(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool hasVisibleZoomSliders() const noexcept;
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
    ana::OfflineScopeSnapshot::BandEnvelopes widebandHistoryEnvelopes;
    std::array<std::array<float, ana::OfflineScopeSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMinimums;
    std::array<std::array<float, ana::OfflineScopeSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMaximums;
    std::array<float, ana::OfflineScopeSnapshot::numChannelModes> widebandColumnMinimums;
    std::array<float, ana::OfflineScopeSnapshot::numChannelModes> widebandColumnMaximums;
    std::array<float, ana::MultibandScope::numBands> bandHeightWeights;
    std::array<bool, ana::MultibandScope::numBands> clearedBands {};
    bool widebandCleared = false;
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
    bool fullSourceView = false;
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

    void setAnalyzerPage(AnaAnalyzerPage page);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onEqualBandHeights;
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
    AnaParameterControl timeNoteControl;
    AnaParameterControl timeBaseControl;
    AnaParameterControl frequencyBlockSizeControl;
    AnaParameterControl frequencyOverlapControl;
    AnaParameterControl frequencyAverageTimeControl;
    AnaParameterControl frequencyFirstSpectrumTypeControl;
    AnaParameterControl frequencySecondSpectrumTypeControl;
    AnaParameterControl frequencySlopeControl;
    AnaParameterControl correlationBlockSizeControl;
    AnaParameterControl correlationOverlapControl;
    AnaParameterControl correlationAverageTimeControl;
    AnaParameterControl correlationSmoothingControl;
    AnaParameterControl correlationFirstSpectrumTypeControl;
    AnaParameterControl correlationSecondSpectrumTypeControl;
    AnaScopeButton addCrossoverButton { "XOV-ADD" };
    AnaScopeButton removeCrossoverButton { "XOV-DEL" };
    AnaScopeButton equalHeightButton { "EQUAL-HEIGHT" };
    AnaScopeButton zoomControlsButton { "ZOOM" };
    AnaScopeButton monitorControlsButton { "MONITOR" };
    AnaScopeButton otherControlsButton { "OTHERS" };
    AnaScopeButton frequencyFilledDisplayButton { "FILLED-DISPLAY" };
    AnaScopeButton frequencySecondSpectrumButton { "2ND-SPECTRUM" };
    AnaScopeButton frequencyAntiAliasButton { "ANTI-ALIAS" };
    AnaScopeButton frequencyRangesButton { "RANGES" };
    AnaScopeButton frequencyHostClearButton { "HOST-CLEAR" };
    AnaScopeButton frequencyCursorButton { "CURSOR" };
    AnaScopeButton frequencyMonitorControlsButton { "MONITOR" };
    AnaScopeButton frequencyZoomControlsButton { "ZOOM" };
    AnaScopeButton correlationFilledDisplayButton { "FILLED-DISPLAY" };
    AnaScopeButton correlationSecondSpectrumButton { "2ND-SPECTRUM" };
    AnaScopeButton correlationHostClearButton { "HOST-CLEAR" };
    AnaScopeButton correlationRangesButton { "RANGES" };
    AnaScopeButton correlationCursorButton { "CURSOR" };
    AnaScopeButton correlationZoomControlsButton { "ZOOM" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> zoomControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monitorControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> otherControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyFilledDisplayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencySecondSpectrumAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyAntiAliasAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyRangesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyHostClearAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyCursorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyMonitorControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> frequencyZoomControlsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationFilledDisplayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationSecondSpectrumAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationHostClearAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationRangesAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationCursorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> correlationZoomControlsAttachment;
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
    AnaAnalyzerPage analyzerPage = AnaAnalyzerPage::scope;
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
    void showAnalyzerSettings(bool shouldShowSettings);
    void showAnalyzerPage(AnaAnalyzerPage page, bool shouldShowSettings);
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
    AnaFrequencyDisplayComponent frequencyDisplay;
    AnaCorrelationDisplayComponent correlationDisplay;
    juce::Label offlineUpdateLabel;
    std::unique_ptr<AnaChoicePrompt> choicePrompt;

    AnaScopeButton frequencyButton { "FREQ" };
    AnaScopeButton phaseButton { "CORR" };
    AnaScopeButton scopeButton { "SCOPE" };
    AnaScopeButton settingsButton { "SETTINGS" };
    AnaScopeButton realtimeButton { "REALTIME" };
    AnaScopeButton offlineButton { "OFFLINE" };
    AnaScopeButton sourceButton { "SOURCE" };
    AnaScopeButton takeButton { "TAKE" };
    AnaScopeButton refreshButton { "REFRESH" };
    AnaScopeButton fullSourceButton { "FSCR" };
    AnaScopeButton clearButton { "CLEAR" };
    AnaScopeButton freezeButton { "FREEZE" };
    juce::Point<int> pendingEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    AnaAnalyzerPage activePage = AnaAnalyzerPage::scope;
    bool showingAnalyzerSettings = false;
    bool scopeFrozen = false;
    std::vector<ana::OfflineSourceTakeChoice> offlineSourceTakeChoices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaAudioProcessorEditor)
};
