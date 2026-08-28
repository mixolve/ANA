#pragma once

#include "../scope/MultibandWaveformProcessor.h"
#include "../freq/FrequencySpectrumProcessor.h"
#include "../corr/StereoCorrelationProcessor.h"

#include <JuceHeader.h>
#include <array>
#include <atomic>

class AnaAudioProcessor final : public juce::AudioProcessor,
                                private juce::VST3ClientExtensions
#if JucePlugin_Enable_ARA
                              , public juce::AudioProcessorARAExtension
#endif
{
public:
    static constexpr const char* scopeTimeParameterId = "scopeTimeMs";
    static constexpr const char* scopeTimeBaseParameterId = "scopeTimeBase";
    static constexpr const char* scopeNoteLengthParameterId = "scopeNoteLength";
    static constexpr const char* scopeStyleParameterId = "scopeStyle";
    static constexpr const char* scopeOpacityParameterId = "scopeOpacity";
    static constexpr const char* scopeZoomControlsParameterId = "scopeZoomControls";
    static constexpr const char* scopeMonitorControlsParameterId = "scopeMonitorControls";
    static constexpr const char* scopeOtherControlsParameterId = "scopeOtherControls";
    static constexpr const char* frequencyBlockSizeParameterId = "frequencyBlockSize";
    static constexpr const char* frequencyOverlapParameterId = "frequencyOverlap";
    static constexpr const char* frequencyAverageTimeParameterId = "frequencyAverageTime";
    static constexpr const char* frequencyFilledDisplayParameterId = "frequencyFilledDisplay";
    static constexpr const char* frequencySecondSpectrumParameterId = "frequencySecondSpectrum";
    static constexpr const char* frequencyFirstSpectrumTypeParameterId = "frequencyFirstSpectrumType";
    static constexpr const char* frequencySecondSpectrumTypeParameterId = "frequencySecondSpectrumType";
    static constexpr const char* frequencyAntiAliasParameterId = "frequencyAntiAlias";
    static constexpr const char* frequencySlopeParameterId = "frequencySlope";
    static constexpr const char* frequencyLowParameterId = "frequencyLow";
    static constexpr const char* frequencyHighParameterId = "frequencyHigh";
    static constexpr const char* frequencyRangeLowParameterId = "frequencyRangeLow";
    static constexpr const char* frequencyRangeHighParameterId = "frequencyRangeHigh";
    static constexpr const char* frequencyRangesVisibleParameterId = "frequencyRangesVisible";
    static constexpr const char* frequencyHostClearParameterId = "frequencyHostClear";
    static constexpr const char* frequencyCursorReadoutParameterId = "frequencyCursorReadout";
    static constexpr const char* frequencyMonitorControlsParameterId = "frequencyMonitorControls";
    static constexpr const char* frequencyZoomControlsParameterId = "frequencyZoomControls";
    static constexpr const char* frequencyChannelModeParameterId = "frequencyChannelMode";
    static constexpr const char* frequencySplitViewParameterId = "frequencySplitView";
    static constexpr const char* correlationBlockSizeParameterId = "correlationBlockSize";
    static constexpr const char* correlationOverlapParameterId = "correlationOverlap";
    static constexpr const char* correlationAverageTimeParameterId = "correlationAverageTime";
    static constexpr const char* correlationSmoothingParameterId = "correlationSmoothing";
    static constexpr const char* correlationFilledDisplayParameterId = "correlationFilledDisplay";
    static constexpr const char* correlationSecondSpectrumParameterId = "correlationSecondSpectrum";
    static constexpr const char* correlationFirstSpectrumTypeParameterId = "correlationFirstSpectrumType";
    static constexpr const char* correlationSecondSpectrumTypeParameterId = "correlationSecondSpectrumType";
    static constexpr const char* correlationHostClearParameterId = "correlationHostClear";
    static constexpr const char* correlationRangesVisibleParameterId = "correlationRangesVisible";
    static constexpr const char* correlationCursorReadoutParameterId = "correlationCursorReadout";
    static constexpr const char* correlationZoomControlsParameterId = "correlationZoomControls";
    static constexpr const char* correlationModeParameterId = "correlationMode";
    static constexpr const char* correlationLowParameterId = "correlationLow";
    static constexpr const char* correlationHighParameterId = "correlationHigh";
    static constexpr const char* correlationRangeLowParameterId = "correlationRangeLow";
    static constexpr const char* correlationRangeHighParameterId = "correlationRangeHigh";
    static constexpr const char* offlineModeParameterId = "offlineMode";
    static constexpr const char* activeSplitCountParameterId = "activeSplitCount";
    static constexpr const char* editorWidthStateKey = "ana.editor.width";
    static constexpr const char* editorHeightStateKey = "ana.editor.height";
    static constexpr const char* offlineSourceStateKey = "ana.offline.source";
    static constexpr const char* offlineTakeStateKey = "ana.offline.take";
    static constexpr const char* scopeSingleViewStateKey = "ana.scope.singleView";
    static constexpr const char* scopeFullSourceStateKey = "ana.scope.fullSource";
    static constexpr const char* analyzerPageStateKey = "ana.analyzer.page";
    inline static constexpr std::array<const char*, ana::dsp::Crossover::numSplits> crossoverParameterIds {
        "xover1", "xover2", "xover3", "xover4", "xover5"
    };
    inline static constexpr std::array<const char*, ana::MultibandScope::numBands> scopeChannelModeParameterIds {
        "scopeBand1Mode", "scopeBand2Mode", "scopeBand3Mode",
        "scopeBand4Mode", "scopeBand5Mode", "scopeBand6Mode"
    };
    inline static constexpr std::array<const char*, ana::MultibandScope::numBands> scopeVerticalZoomParameterIds {
        "scopeBand1Zoom", "scopeBand2Zoom", "scopeBand3Zoom",
        "scopeBand4Zoom", "scopeBand5Zoom", "scopeBand6Zoom"
    };
    inline static constexpr std::array<const char*, ana::MultibandScope::numBands> scopeNormalizeParameterIds {
        "scopeBand1Normalize", "scopeBand2Normalize", "scopeBand3Normalize",
        "scopeBand4Normalize", "scopeBand5Normalize", "scopeBand6Normalize"
    };

    AnaAudioProcessor();
    ~AnaAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    ana::MultibandScope& getMultibandScope() noexcept { return multibandScope; }
    ana::freq::FrequencySpectrumProcessor& getFrequencySpectrum() noexcept { return frequencySpectrum; }
    const ana::freq::FrequencySpectrumProcessor& getFrequencySpectrum() const noexcept { return frequencySpectrum; }
    void clearFrequencySpectrum() noexcept { frequencySpectrum.requestClear(); }
    ana::corr::StereoCorrelationProcessor& getCorrelationSpectrum() noexcept { return correlationProcessor; }
    const ana::corr::StereoCorrelationProcessor& getCorrelationSpectrum() const noexcept { return correlationProcessor; }
    void clearCorrelationSpectrum() noexcept { correlationProcessor.requestClear(); }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    bool isOfflineMode() const noexcept;
    bool isARAAvailable() const noexcept;
    void requestOfflineAnalysis(size_t columnCount, bool forceRefresh = false);
    std::shared_ptr<const ana::OfflineScopeSnapshot> getOfflineScopeSnapshot() const;
    std::vector<ana::OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;
    juce::String getSelectedOfflineSourceId() const;
    juce::String getSelectedOfflineTakeId() const;
    double getScopeTimeMilliseconds() const noexcept;
    bool isScopeTimeNoteBased() const noexcept;
    bool isScopeFilledStyle() const noexcept;
    float getScopeOpacity() const noexcept;
    bool areScopeZoomControlsVisible() const noexcept;
    bool areScopeMonitorControlsVisible() const noexcept;
    bool areScopeOtherControlsVisible() const noexcept;
    size_t getActiveSplitCount() const noexcept;
    ana::ScopeChannelMode getScopeChannelMode(size_t bandIndex) const noexcept;
    float getScopeVerticalZoomDecibels(size_t bandIndex) const noexcept;
    bool isScopeBandNormalized(size_t bandIndex) const noexcept;
    ana::dsp::Crossover::SplitFrequencies getCrossoverFrequencies() const noexcept;
    juce::Point<int> getLastEditorSize() const noexcept;
    int getScopeSingleViewBand() const noexcept;
    bool isScopeFullSourceView() const noexcept;
    bool isFrequencyPageSelected() const noexcept;
    int getAnalyzerPageState() const noexcept;
    void setLastEditorSize(int width, int height) noexcept;
    void setFrequencyPageSelected(bool shouldSelectFrequency);
    void setAnalyzerPageState(int page);
    void setActiveSplitCount(size_t splitCount);
    void setOfflineMode(bool shouldUseOfflineMode);
    void setSelectedOfflineSourceId(const juce::String& sourceId);
    void setSelectedOfflineTakeId(const juce::String& takeId);
    void setScopeSingleViewBand(int bandIndex);
    void setScopeFullSourceView(bool shouldShowFullSource);
    void setScopeChannelMode(size_t bandIndex, ana::ScopeChannelMode mode);
    void setScopeVerticalZoomDecibels(size_t bandIndex, float decibels);
    void setScopeBandNormalized(size_t bandIndex, bool shouldNormalize);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;
    void setIHostApplication(Steinberg::FUnknown* hostApplication) override;
    std::vector<ana::OfflineSourceTakeChoice> getReaperSourceTakeChoices() const;

    juce::AudioProcessorValueTreeState parameters;
    ana::MultibandScope multibandScope;
    ana::freq::FrequencySpectrumProcessor frequencySpectrum;
    ana::corr::StereoCorrelationProcessor correlationProcessor;
    std::atomic<double> hostTempoBpm { 120.0 };
    bool hostWasPlaying = false;
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
    mutable juce::CriticalSection offlineSelectionLock;
    juce::String selectedOfflineSourceId;
    juce::String selectedOfflineTakeId;
    mutable juce::CriticalSection reaperHostLock;
    void* reaperHostApplication = nullptr;
};
