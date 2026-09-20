#pragma once

#include "scop/Processor.h"
#include "shared/scop/Crossover.h"
#include "shared/analyzer/Page.h"
#include "shared/spec/MonitorMode.h"
#include "spec/Processor.h"
#include "corr/Processor.h"
#include "shared/lvls/Processor.h"

#include <cstddef>
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class PluginProcessor final : public juce::AudioProcessor,
                                private juce::VST3ClientExtensions
{
public:
    static constexpr const char* scopTimeParameterId = "scopTimeMs";
    static constexpr const char* scopTimeBaseParameterId = "scopTimeBase";
    static constexpr const char* scopNoteLengthParameterId = "scopNoteLength";
    static constexpr const char* scopStyleParameterId = "scopStyle";
    static constexpr const char* scopOpacityParameterId = "scopOpacity";
    static constexpr const char* scopZoomControlsParameterId = "scopZoomControls";
    static constexpr const char* scopMonitorControlsParameterId = "scopMonitorControls";
    static constexpr const char* scopToolsParameterId = "scopTools";
    static constexpr const char* scopLeftToRightParameterId = "scopLeftToRight";
    static constexpr const char* specFftSizeParameterId = "specFftSize";
    static constexpr const char* specFftOverlapParameterId = "specFftOverlap";
    static constexpr const char* specMapTimeOverlapParameterId = "specMapTimeOverlap";
    static constexpr const char* specAverageTimeParameterId = "specAverageTime";
    static constexpr const char* specSmoothingParameterId = "specSmoothing";
    static constexpr const char* specFrequencyScaleParameterId = "specFrequencyScale";
    static constexpr const char* specFilledDisplayParameterId = "specFilledDisplay";
    static constexpr const char* specSecondGraphParameterId = "specSecondGraph";
    static constexpr const char* specFirstGraphTypeParameterId = "specFirstGraphType";
    static constexpr const char* specSecondGraphTypeParameterId = "specSecondGraphType";
    static constexpr const char* specAntiAliasParameterId = "specAntiAlias";
    static constexpr const char* specHighQualityRenderingParameterId = "specHighQualityRendering";
    static constexpr const char* specMapLeftToRightParameterId = "specMapLeftToRight";
    static constexpr const char* specSlopeParameterId = "specSlope";
    static constexpr const char* specLowParameterId = "specLow";
    static constexpr const char* specHighParameterId = "specHigh";
    static constexpr const char* specRangeLowParameterId = "specRangeLow";
    static constexpr const char* specRangeHighParameterId = "specRangeHigh";
    static constexpr const char* specRangesVisibleParameterId = "specRangesVisible";
    static constexpr const char* specClearOnPlayParameterId = "specClearOnPlay";
    static constexpr const char* specCursorReadoutParameterId = "specCursorReadout";
    static constexpr const char* specMonitorControlsParameterId = "specMonitorControls";
    static constexpr const char* specZoomControlsParameterId = "specZoomControls";
    static constexpr const char* specMonitorModeParameterId = "specMonitorMode";
    static constexpr const char* specSplitViewParameterId = "specSplitView";
    static constexpr const char* corrFftSizeParameterId = "corrFftSize";
    static constexpr const char* corrFftOverlapParameterId = "corrFftOverlap";
    static constexpr const char* corrAverageTimeParameterId = "corrAverageTime";
    static constexpr const char* corrSmoothingParameterId = "corrSmoothing";
    static constexpr const char* corrFrequencyScaleParameterId = "corrFrequencyScale";
    static constexpr const char* corrFilledDisplayParameterId = "corrFilledDisplay";
    static constexpr const char* corrSecondGraphParameterId = "corrSecondGraph";
    static constexpr const char* corrFirstGraphTypeParameterId = "corrFirstGraphType";
    static constexpr const char* corrSecondGraphTypeParameterId = "corrSecondGraphType";
    static constexpr const char* corrClearOnPlayParameterId = "corrClearOnPlay";
    static constexpr const char* corrRangesVisibleParameterId = "corrRangesVisible";
    static constexpr const char* corrCursorReadoutParameterId = "corrCursorReadout";
    static constexpr const char* corrZoomControlsParameterId = "corrZoomControls";
    static constexpr const char* corrModeParameterId = "corrMode";
    static constexpr const char* corrLowParameterId = "corrLow";
    static constexpr const char* corrHighParameterId = "corrHigh";
    static constexpr const char* corrRangeLowParameterId = "corrRangeLow";
    static constexpr const char* corrRangeHighParameterId = "corrRangeHigh";
    static constexpr const char* lvlsWidthParameterId = "lvlsWidth";
    static constexpr const char* lvlsPeakRangeHighParameterId = "lvlsPeakRangeHigh";
    static constexpr const char* lvlsPeakRangeLowParameterId = "lvlsPeakRangeLow";
    static constexpr const char* lvlsRmsWindowMsParameterId = "lvlsRmsWindowMs";
    static constexpr const char* lvlsPeakHoldMsParameterId = "lvlsPeakHoldMs";
    static constexpr const char* lvlsLoudnessRangeHighParameterId = "lvlsLoudnessRangeHigh";
    static constexpr const char* lvlsLoudnessRangeLowParameterId = "lvlsLoudnessRangeLow";
    static constexpr const char* lvlsClearOnPlayParameterId = "lvlsClearOnPlay";
    static constexpr const char* lvlsPeakRmsVisibleParameterId = "lvlsPeakRmsVisible";
    static constexpr const char* lvlsLoudnessVisibleParameterId = "lvlsLoudnessVisible";
    static constexpr const char* lvlsHistoryVisibleParameterId = "lvlsHistoryVisible";
    static constexpr const char* lvlsHistoryMomentaryVisibleParameterId = "lvlsHistoryMomentaryVisible";
    static constexpr const char* lvlsHistoryShortTermVisibleParameterId = "lvlsHistoryShortTermVisible";
    static constexpr const char* lvlsHistoryIntegratedVisibleParameterId = "lvlsHistoryIntegratedVisible";
    static constexpr const char* lvlsHistoryZoomParameterId = "lvlsHistoryZoom";
    static constexpr const char* crossoverCountParameterId = "crossoverCount";
    static constexpr const char* editorWidthStateKey = "ana.editor.width";
    static constexpr const char* editorHeightStateKey = "ana.editor.height";
    static constexpr const char* scopSingleViewStateKey = "ana.scop.singleView";
    static constexpr const char* scopFullSourceStateKey = "ana.scop.fullSource";
    static constexpr const char* analyzerPageStateKey = "ana.analyzer.page";
    static constexpr const char* specViewModeStateKey = "ana.spec.viewMode";
    inline static constexpr std::array<const char*, 3> lvlsSectionWeightStateKeys {
        "ana.lvls.section1", "ana.lvls.section2", "ana.lvls.section3"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numCrossovers> crossoverParameterIds {
        "crossover1", "crossover2", "crossover3", "crossover4", "crossover5"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopChannelModeParameterIds {
        "scopBand1Mode", "scopBand2Mode", "scopBand3Mode",
        "scopBand4Mode", "scopBand5Mode", "scopBand6Mode"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopVerticalZoomParameterIds {
        "scopBand1Zoom", "scopBand2Zoom", "scopBand3Zoom",
        "scopBand4Zoom", "scopBand5Zoom", "scopBand6Zoom"
    };

    PluginProcessor();
    ~PluginProcessor() override;

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

    ana::MultibandScop& getMultibandScop() noexcept { return multibandScop; }
    ana::spec::SpecProcessor& getSpecProcessor() noexcept { return specProcessor; }
    const ana::spec::SpecProcessor& getSpecProcessor() const noexcept { return specProcessor; }
    void clearSpecProcessor() noexcept { specProcessor.requestClear(); }
    ana::corr::CorrProcessor& getCorrProcessor() noexcept { return corrProcessor; }
    const ana::corr::CorrProcessor& getCorrProcessor() const noexcept { return corrProcessor; }
    void clearCorrProcessor() noexcept { corrProcessor.requestClear(); }
    ana::lvls::LvlsProcessor& getLvlsProcessor() noexcept { return lvlsProcessor; }
    const ana::lvls::LvlsProcessor& getLvlsProcessor() const noexcept { return lvlsProcessor; }
    void clearLvlsProcessor() noexcept { lvlsProcessor.requestClear(); }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters; }
    double getScopTimeMilliseconds() const noexcept;
    bool isScopTimeNoteBased() const noexcept;
    bool isScopFilledStyle() const noexcept;
    float getScopOpacity() const noexcept;
    bool areScopZoomControlsVisible() const noexcept;
    bool areScopMonitorControlsVisible() const noexcept;
    bool areScopToolsVisible() const noexcept;
    size_t getCrossoverCount() const noexcept;
    ana::ScopChannelMode getScopChannelMode(size_t bandIndex) const noexcept;
    float getScopVerticalZoomDecibels(size_t bandIndex) const noexcept;
    ana::dsp::LinkwitzRileyCrossover::CrossoverFrequencies getCrossoverFrequencies() const noexcept;
    juce::Point<int> getLastEditorSize() const noexcept;
    std::array<float, 3> getLvlsSectionWeights() const noexcept;
    int getScopSingleViewBand() const noexcept;
    bool isScopFullSourceView() const noexcept;
    ana::AnalyzerPage getAnalyzerPageState() const noexcept;
    bool isSpecMapView() const noexcept;
    void setSpecMonitorMode(int mode);
    void setCorrMode(int mode);
    void setLastEditorSize(int width, int height) noexcept;
    void setLvlsSectionWeights(const std::array<float, 3>& weights);
    void setAnalyzerPageState(ana::AnalyzerPage page);
    void setSpecMapView(bool shouldUseMap);
    void setCrossoverCount(size_t crossoverCount);
    void setScopSingleViewBand(int bandIndex);
    void setScopFullSourceView(bool shouldShowFullSource);
    void setScopChannelMode(size_t bandIndex, ana::ScopChannelMode mode);
    void setScopVerticalZoomDecibels(size_t bandIndex, float decibels);

private:
    bool isParameterEnabled(const char* parameterId) const noexcept;
    void constrainCorrRangeForMode(int mode);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;
    void setIHostApplication(Steinberg::FUnknown* hostApplication) override;

    juce::AudioProcessorValueTreeState parameters;
    ana::MultibandScop multibandScop;
    ana::spec::SpecProcessor specProcessor;
    ana::corr::CorrProcessor corrProcessor;
    ana::lvls::LvlsProcessor lvlsProcessor;
    std::atomic<double> hostTempoBpm { 120.0 };
    std::atomic<ana::AnalyzerPage> activeAnalyzerPage { ana::AnalyzerPage::spec };
    bool hostWasPlaying = false;
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
};
