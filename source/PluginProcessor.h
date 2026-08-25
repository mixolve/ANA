#pragma once

#include "MultibandScope.h"

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
    static constexpr const char* scopeStyleParameterId = "scopeStyle";
    static constexpr const char* scopeOpacityParameterId = "scopeOpacity";
    static constexpr const char* scopeZoomControlsParameterId = "scopeZoomControls";
    static constexpr const char* scopeMonitorControlsParameterId = "scopeMonitorControls";
    static constexpr const char* scopeOtherControlsParameterId = "scopeOtherControls";
    static constexpr const char* alwaysSecondTakeParameterId = "alwaysSecondTake";
    static constexpr const char* offlineModeParameterId = "offlineMode";
    static constexpr const char* activeSplitCountParameterId = "activeSplitCount";
    static constexpr const char* editorWidthStateKey = "ana.editor.width";
    static constexpr const char* editorHeightStateKey = "ana.editor.height";
    static constexpr const char* offlineSourceStateKey = "ana.offline.source";
    static constexpr const char* offlineTakeStateKey = "ana.offline.take";
    static constexpr const char* scopeSingleViewStateKey = "ana.scope.singleView";
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
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    bool isOfflineMode() const noexcept;
    bool isARAAvailable() const noexcept;
    void requestOfflineAnalysis(size_t columnCount, bool forceRefresh = false);
    std::shared_ptr<const ana::OfflineScopeSnapshot> getOfflineScopeSnapshot() const;
    std::vector<ana::OfflineSourceTakeChoice> getOfflineSourceTakeChoices() const;
    juce::String getSelectedOfflineSourceId() const;
    juce::String getSelectedOfflineTakeId() const;
    double getScopeTimeMilliseconds() const noexcept;
    bool isScopeFilledStyle() const noexcept;
    float getScopeOpacity() const noexcept;
    bool areScopeZoomControlsVisible() const noexcept;
    bool areScopeMonitorControlsVisible() const noexcept;
    bool areScopeOtherControlsVisible() const noexcept;
    bool shouldAlwaysUseSecondOfflineTake() const noexcept;
    size_t getActiveSplitCount() const noexcept;
    ana::ScopeChannelMode getScopeChannelMode(size_t bandIndex) const noexcept;
    float getScopeVerticalZoomDecibels(size_t bandIndex) const noexcept;
    bool isScopeBandNormalized(size_t bandIndex) const noexcept;
    ana::dsp::Crossover::SplitFrequencies getCrossoverFrequencies() const noexcept;
    juce::Point<int> getLastEditorSize() const noexcept;
    int getScopeSingleViewBand() const noexcept;
    void setLastEditorSize(int width, int height) noexcept;
    void setActiveSplitCount(size_t splitCount);
    void setOfflineMode(bool shouldUseOfflineMode);
    void setSelectedOfflineSourceId(const juce::String& sourceId);
    void setSelectedOfflineTakeId(const juce::String& takeId);
    void setAlwaysUseSecondOfflineTake(bool shouldUseSecondTake);
    void setScopeSingleViewBand(int bandIndex);
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
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
    mutable juce::CriticalSection offlineSelectionLock;
    juce::String selectedOfflineSourceId;
    juce::String selectedOfflineTakeId;
    mutable juce::CriticalSection reaperHostLock;
    void* reaperHostApplication = nullptr;
};
