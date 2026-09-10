#pragma once

#include "../scop/MultibandWaveformProcessor.h"
#include "../ara/OfflineAnalysisData.h"
#include "../lvls/MeterProcessor.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class PluginProcessor;

class EllipsisLabel final : public juce::Label
{
public:
    void setDrawBackground(const bool shouldDraw) noexcept
    {
        drawBackground = shouldDraw;
        repaint();
    }
    void setTextVerticalOffset(const int offset) noexcept
    {
        textVerticalOffset = offset;
        repaint();
    }
    void paint(juce::Graphics& graphics) override;

private:
    int textVerticalOffset = 0;
    bool drawBackground = true;
};

class ControlButton final : public juce::Button
{
public:
    explicit ControlButton(juce::String text);

    void paintButton(juce::Graphics& graphics,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;
    int getPreferredWidth() const noexcept;

private:
    juce::Image symbolImage;
    bool iconButton = false;
};

class AboutPopup final : public juce::Component
{
public:
    explicit AboutPopup(std::function<void()> closeCallback);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void requestClose();
    static juce::URL createOfflineManualUrl();

    juce::HyperlinkButton webLink { "WEB", juce::URL("https://mixolve.cc/") };
    juce::HyperlinkButton manualLink { "MANUAL", juce::URL() };
    std::vector<std::unique_ptr<EllipsisLabel>> textLabels;
    std::vector<juce::Component*> contentRows;
    ControlButton okButton { "OK" };
    std::function<void()> onClose;
};

class SliderLookAndFeel final : public juce::LookAndFeel_V4
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

class ParameterControl final : public juce::Component, private juce::Timer
{
public:
    using Formatter = std::function<juce::String(double)>;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterId,
                        juce::String title,
                        Formatter formatter);
    ~ParameterControl() override;

    juce::Slider& getSlider() noexcept { return slider; }
    void setInteractionEnabled(bool shouldEnable);
    bool isInteractionEnabled() const noexcept { return interactionEnabled; }
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
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void(ParameterControl&)> onFocusRequested;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void()> onValueChanged;

private:
    enum class PressRegion { none, title, value };

    void timerCallback() override;
    void showValueEditor();
    void hideValueEditor(bool discardChanges);
    void resetToDefault();

    juce::String titleText;
    Formatter valueFormatter;
    SliderLookAndFeel sliderLookAndFeel;
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
    PressRegion hoverRegion = PressRegion::none;
};

class ChoicePopup final : public juce::Component
{
public:
    ChoicePopup(juce::Rectangle<int> anchorBounds,
                    juce::StringArray choices,
                    int selectedIndex,
                    std::function<void(int)> onSelect,
                    std::function<void()> onClose);

    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void choose(int index);
    void close();

    juce::Rectangle<int> anchorBounds;
    juce::StringArray choices;
    std::vector<std::unique_ptr<ControlButton>> choiceButtons;
    juce::Rectangle<int> panelBounds;
    std::function<void(int)> onSelect;
    std::function<void()> onClose;
    bool closing = false;
};

class RangeSlider final : public juce::Component
{
public:
    enum class Orientation { horizontal, vertical };

    explicit RangeSlider(Orientation newOrientation = Orientation::horizontal)
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

class SpectrumView final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView(PluginProcessor& processorRef);

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
                      juce::Colour lineColour, juce::Colour fillColour) const;
    static float frequencyToNormalised(float frequency) noexcept;
    static float normalisedToFrequency(float normalised) noexcept;

    PluginProcessor& processor;
    ParameterControl frequencyLowControl;
    ParameterControl frequencyHighControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    EllipsisLabel cursorReadoutLabel;
    EllipsisLabel cursorNoteReadoutLabel;
    EllipsisLabel cursorVerticalReadoutLabel;
    std::array<std::unique_ptr<ControlButton>, 7> monitorButtons;
    ControlButton splitButton { "SPLIT" };
    RangeSlider frequencyRangeSlider;
    RangeSlider magnitudeRangeSlider { RangeSlider::Orientation::vertical };
    std::vector<float> primarySpectrum;
    std::vector<float> secondarySpectrum;
    uint64_t displayedRevision = 0;
    uint64_t displayedOfflineRevision = 0;
    bool synchronisingRanges = false;
    juce::Point<float> cursorPosition;
    bool cursorInside = false;
    bool splitButtonFits = true;
    float lastCursorFrequency = 0.0f;
};

class CorrelationView final : public juce::Component, private juce::Timer
{
public:
    explicit CorrelationView(PluginProcessor& processorRef);

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
                         double sampleRate, int fftSize, juce::Colour lineColour,
                         juce::Colour fillColour);
    juce::Rectangle<float> getPlotBounds() const noexcept;
    static float frequencyToNormalised(float frequency) noexcept;
    static float normalisedToFrequency(float normalised) noexcept;

    PluginProcessor& processor;
    ParameterControl frequencyLowControl;
    ParameterControl frequencyHighControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    EllipsisLabel cursorReadoutLabel;
    EllipsisLabel cursorVerticalReadoutLabel;
    ControlButton phaseModeButton { "PHASE" };
    ControlButton amplitudeModeButton { "AMPLITUDE" };
    RangeSlider frequencyRangeSlider;
    RangeSlider correlationRangeSlider { RangeSlider::Orientation::vertical };
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

enum class AnalyzerPage { scope, frequency, correlation, level };

class MeterView final : public juce::Component, private juce::Timer
{
public:
    explicit MeterView(PluginProcessor& processorRef);

    void centerParts();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void layoutPeakModeButtons();
    int getMeterWidth() const noexcept;
    std::array<bool, 3> getVisibleParts() const noexcept;
    std::array<int, 3> getMinimumPartWidths() const noexcept;
    juce::Rectangle<float> getPlotBounds() const noexcept;
    std::array<juce::Rectangle<int>, 3> getPartBounds() const noexcept;
    int findPartSeparator(int x) const noexcept;

    PluginProcessor& processor;
    std::array<float, 4> peakValues { -120.0f, -120.0f, -120.0f, -120.0f };
    std::array<float, 4> rmsValues { -120.0f, -120.0f, -120.0f, -120.0f };
    std::array<float, 4> peakMaximumValues { -120.0f, -120.0f, -120.0f, -120.0f };
    std::array<float, 4> peakHoldValues { -120.0f, -120.0f, -120.0f, -120.0f };
    std::array<float, 4> rmsMaximumValues { -120.0f, -120.0f, -120.0f, -120.0f };
    float momentaryLufs = -120.0f;
    float shortTermLufs = -120.0f;
    float integratedLufs = -120.0f;
    float momentaryMaximumLufs = -120.0f;
    float shortTermMaximumLufs = -120.0f;
    float integratedMaximumLufs = -120.0f;
    float loudnessRange = 0.0f;
    std::array<std::vector<float>, ana::lvls::MeterProcessor::historySeriesCount> loudnessHistories;
    uint64_t displayedRealtimeRevision = 0;
    uint64_t displayedOfflineRevision = 0;
    std::array<float, 3> partWeights { 1.0f, 1.0f, 1.0f };
    std::array<int, 3> dragStartWidths {};
    int draggedPartSeparator = -1;
    int draggedLeftPart = -1;
    int draggedRightPart = -1;
    int hoveredPartSeparator = -1;
    int dragStartX = 0;
    ControlButton peakModeButton { "PEAK" };
    ControlButton rmsModeButton { "RMS" };
    ControlButton peakChannelModeButton { "MS" };
    ControlButton historySviewButton { "SVIEW" };
    RangeSlider historyHorizontalZoom;
    RangeSlider historyVerticalZoom { RangeSlider::Orientation::vertical };
    bool showPeakMeter = true;
    bool showMidSideMeters = false;
    bool historySolo = false;
};

class ScopeView final : public juce::Component, private juce::Timer
{
public:
    explicit ScopeView(PluginProcessor& processorRef);

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
    void renderOfflineSnapshot(std::shared_ptr<const ana::OfflineAnalysisSnapshot> snapshot);
    void refreshBandModeButtons();
    void normalizeBandWithZoom(size_t bandIndex);
    std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes> getRealtimeModeSamples(
        size_t bandIndex, size_t sampleIndex) const noexcept;
    std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes> getRealtimeWidebandModeSamples(
        size_t sampleIndex) const noexcept;
    juce::Rectangle<float> getBandBounds(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool shouldShowZoomSliders(size_t bandIndex, size_t activeBandCount) const noexcept;
    bool hasVisibleZoomSliders() const noexcept;
    int findBandSeparator(int y, size_t activeBandCount) const noexcept;
    size_t getWaveformColumnCount() const noexcept;

    PluginProcessor& processor;
    ana::MultibandScope::Snapshot incomingSamples;
    SliderLookAndFeel bandZoomLookAndFeel;
    std::array<std::array<std::unique_ptr<ControlButton>, 6>, ana::MultibandScope::numBands> bandModeButtons;
    std::array<std::unique_ptr<ControlButton>, ana::MultibandScope::numBands> bandClearButtons;
    std::array<std::unique_ptr<ControlButton>, ana::MultibandScope::numBands> bandSingleViewButtons;
    std::array<std::unique_ptr<juce::Slider>, ana::MultibandScope::numBands> bandZoomSliders;
    std::array<std::unique_ptr<EllipsisLabel>, ana::MultibandScope::numBands> bandZoomValueLabels;
    std::array<std::unique_ptr<ControlButton>, ana::MultibandScope::numBands> bandNormalizeButtons;
    std::array<std::unique_ptr<RangeSlider>, ana::MultibandScope::numBands> bandRangeSliders;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> historyChannelModes {};
    std::array<ana::OfflineAnalysisSnapshot::BandEnvelopes, ana::MultibandScope::numBands> historyEnvelopes;
    ana::OfflineAnalysisSnapshot::BandEnvelopes widebandHistoryEnvelopes;
    std::array<std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMinimums;
    std::array<std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes>, ana::MultibandScope::numBands> columnMaximums;
    std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes> widebandColumnMinimums;
    std::array<float, ana::OfflineAnalysisSnapshot::numChannelModes> widebandColumnMaximums;
    std::array<float, ana::MultibandScope::numBands> bandHeightWeights;
    std::array<bool, ana::MultibandScope::numBands> clearedBands {};
    bool widebandCleared = false;
    std::shared_ptr<const ana::OfflineAnalysisSnapshot> displayedOfflineSnapshot;
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

class SettingsPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SettingsPanel(PluginProcessor& processorRef);
    ~SettingsPanel() override;

    void setAnalyzerPage(AnalyzerPage page);
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    std::function<void()> onDisplaySettingsChanged;
    std::function<void()> onEqualBandHeights;
    std::function<void()> onCenterLevelParts;
    std::function<void(ParameterControl&)> onChoiceRequested;

private:
    void timerCallback() override;
    void changeActiveSplitCount(int delta);
    void constrainFrequency(size_t crossoverIndex);
    void refreshExternalState();
    void focusParameterControl(ParameterControl& control);
    void clearFocusedParameterControl();
    void dismissParameterEditors();
    void syncFocusedParameterControl();

    PluginProcessor& processor;
    ParameterControl styleControl;
    ParameterControl opacityControl;
    ParameterControl timeControl;
    ParameterControl timeNoteControl;
    ParameterControl timeBaseControl;
    ParameterControl frequencyBlockSizeControl;
    ParameterControl frequencyOverlapControl;
    ParameterControl frequencyAverageTimeControl;
    ParameterControl frequencySmoothingControl;
    ParameterControl frequencyFirstSpectrumTypeControl;
    ParameterControl frequencySecondSpectrumTypeControl;
    ParameterControl frequencySlopeControl;
    ParameterControl correlationBlockSizeControl;
    ParameterControl correlationOverlapControl;
    ParameterControl correlationAverageTimeControl;
    ParameterControl correlationSmoothingControl;
    ParameterControl correlationFirstSpectrumTypeControl;
    ParameterControl correlationSecondSpectrumTypeControl;
    ParameterControl levelMeterWidthControl;
    ParameterControl levelPeakHighControl;
    ParameterControl levelPeakLowControl;
    ParameterControl levelRmsWindowControl;
    ParameterControl levelPeakHoldTimeControl;
    ParameterControl levelLufsHighControl;
    ParameterControl levelLufsLowControl;
    ControlButton addCrossoverButton { "XOV-ADD" };
    ControlButton removeCrossoverButton { "XOV-DEL" };
    ControlButton equalHeightButton { "EQUAL-HEIGHT" };
    ControlButton zoomControlsButton { "ZOOM" };
    ControlButton monitorControlsButton { "MONITOR" };
    ControlButton otherControlsButton { "OTHERS" };
    ControlButton frequencyFilledDisplayButton { "FILLED-DISPLAY" };
    ControlButton frequencySecondSpectrumButton { "2ND-SPECTRUM" };
    ControlButton frequencyAntiAliasButton { "ANTI-ALIAS" };
    ControlButton frequencyRangesButton { "RANGES" };
    ControlButton frequencyHostClearButton { "HOST-CLEAR" };
    ControlButton frequencyCursorButton { "CURSOR" };
    ControlButton frequencyMonitorControlsButton { "MONITOR" };
    ControlButton frequencyZoomControlsButton { "ZOOM" };
    ControlButton correlationFilledDisplayButton { "FILLED-DISPLAY" };
    ControlButton correlationSecondSpectrumButton { "2ND-SPECTRUM" };
    ControlButton correlationHostClearButton { "HOST-CLEAR" };
    ControlButton correlationRangesButton { "RANGES" };
    ControlButton correlationCursorButton { "CURSOR" };
    ControlButton correlationZoomControlsButton { "ZOOM" };
    ControlButton centerLevelPartsButton { "CENTER-PARTS" };
    ControlButton levelHostResetButton { "RESET-HOST" };
    ControlButton levelPeakVisibleButton { "PEAK/RMS" };
    ControlButton levelLoudnessVisibleButton { "LOUDNESS" };
    ControlButton levelHistoryVisibleButton { "HISTORY" };
    ControlButton levelHistoryMomentaryButton { "M" };
    ControlButton levelHistoryShortTermButton { "S" };
    ControlButton levelHistoryIntegratedButton { "I" };
    ControlButton levelHistoryZoomButton { "ZOOM" };
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
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHostResetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelPeakVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelLoudnessVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHistoryVisibleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHistoryMomentaryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHistoryShortTermAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHistoryIntegratedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> levelHistoryZoomAttachment;
    std::array<std::unique_ptr<ParameterControl>, ana::dsp::Crossover::numSplits> crossoverControls;
    SliderLookAndFeel focusedControlLookAndFeel;
    juce::Slider focusedParameterControl;
    ParameterControl* focusedParameterTarget = nullptr;
    juce::Component settingsContent;
    juce::Viewport settingsViewport;
    EllipsisLabel generalHeadingLabel;
    EllipsisLabel controlsVisibilityHeadingLabel;
    EllipsisLabel realtimeHeadingLabel;
    AnalyzerPage analyzerPage = AnalyzerPage::scope;
    bool constrainingFrequency = false;
    bool updatingFocusedParameterControl = false;
};

class PluginEditor final : public juce::AudioProcessorEditor
#if JucePlugin_Enable_ARA
                                    , public juce::AudioProcessorEditorARAExtension
#endif
                                    , private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor& processorRef);
    ~PluginEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showAnalyzerSettings(bool shouldShowSettings);
    void showAnalyzerPage(AnalyzerPage page, bool shouldShowSettings);
    void showChoicePrompt(ParameterControl& control);
    void showChoicePrompt(juce::Rectangle<int> anchorBounds,
                          juce::StringArray choices,
                          int selectedIndex,
                          std::function<void(int)> onSelect);
    void showOfflineSourcePrompt();
    void showOfflineTakePrompt();
    void refreshOfflineSelectionButtons();
    void dismissChoicePrompt();
    void showAboutPopup();
    void dismissAboutPopup();
    void timerCallback() override;

    PluginProcessor& audioProcessor;
    ScopeView scopeDisplay;
    SettingsPanel settingsComponent;
    SpectrumView frequencyDisplay;
    CorrelationView correlationDisplay;
    MeterView levelDisplay;
    EllipsisLabel offlineUpdateLabel;
    std::unique_ptr<ChoicePopup> choicePrompt;
    std::unique_ptr<AboutPopup> aboutPopup;
    std::unique_ptr<juce::ResizableEdgeComponent> rightEdgeResizer;
    std::unique_ptr<juce::ResizableEdgeComponent> bottomEdgeResizer;

    ControlButton frequencyButton { "FREQ" };
    ControlButton correlationButton { "CORR" };
    ControlButton levelButton { "LVLS" };
    ControlButton scopeButton { "SCOP" };
    ControlButton settingsButton { "gearshape" };
    ControlButton realtimeButton { "REALTIME" };
    ControlButton offlineButton { "OFFLINE" };
    ControlButton sourceButton { "SOURCE" };
    ControlButton takeButton { "TAKE" };
    ControlButton refreshButton { "arrow.trianglehead.2.clockwise" };
    ControlButton fullSourceButton { "FSCR" };
    ControlButton clearButton { "CLEAR" };
    ControlButton freezeButton { "snowflake" };
    ControlButton controlsButton { "chevron.forward.2" };
    ControlButton mixolveButton { "info.circle" };
    juce::Point<int> pendingEditorSize;
    double editorSizeSaveDeadlineMilliseconds = 0.0;
    double offlineUpdateStatusMinimumEndMilliseconds = 0.0;
    bool editorSizeSavePending = false;
    AnalyzerPage activePage = AnalyzerPage::frequency;
    bool showingAnalyzerSettings = false;
    bool mainControlsVisible = false;
    bool scopeFrozen = false;
    std::vector<ana::OfflineSourceTakeChoice> offlineSourceTakeChoices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
