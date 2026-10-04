#pragma once

#include "shared/scop/Crossover.h"
#include "shared/scop/Settings.h"
#include "shared/shell/Page.h"
#include "offline/shell/AnalysisResult.h"
#include "offline/shell/SourceChoice.h"
#include "shared/spec/MonitorMode.h"
#include "shared/spec/Processor.h"
#include "shared/corr/Processor.h"
#include "shared/lvls/Processor.h"
#include "ReaperBridge.h"
#include "offline/shell/AnalysisWorker.h"

#include <cstddef>
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "realtime/scop/Processor.h"

class OscController;
struct OscSettings;

class PluginProcessor final : public juce::AudioProcessor,
                                private juce::VST3ClientExtensions
                              , public juce::AudioProcessorARAExtension
{
public:
    static constexpr const char* analysisModeParameterId = "mode";
    static constexpr const char* scopLeftToRightParameterId = "scop/left-to-right";
    static constexpr const char* specResetClickParameterId = "spec/freq/reset-click";
    static constexpr const char* specSplitViewParameterId = "spec/freq/split-view";
    static constexpr const char* corrResetClickParameterId = "corr/phase/reset-click";
    static constexpr const char* lvlsResetClickParameterId = "lvls/peak-rms/reset-click";
    static constexpr const char* oscEnabledStateKey = "ana.osc.enabled";
    static constexpr const char* oscInputPortStateKey = "ana.osc.inputPort";
    static constexpr const char* oscOutputHostStateKey = "ana.osc.outputHost";
    static constexpr const char* oscOutputPortStateKey = "ana.osc.outputPort";
    bool isOfflineAvailable() const noexcept;
    bool isOfflineMode() const noexcept;
    void setOfflineMode(bool offline);
    static constexpr const char* moduleParameterId = "module";
    static constexpr const char* cleanViewParameterId = "clean-view";
    static constexpr const char* specViewParameterId = "spec/view";
    static constexpr const char* lvlsLoudnessWidthParameterId = "lvls/loudness/width";
    static constexpr const char* lvlsLoudnessClearOnPlayParameterId = "lvls/loudness/clear-on-play";
    static constexpr const char* lvlsLoudnessResetClickParameterId = "lvls/loudness/reset-click";
    static constexpr const char* lvlsHistoryClearOnPlayParameterId = "lvls/history/clear-on-play";
    static constexpr const char* lvlsHistoryResetClickParameterId = "lvls/history/reset-click";
    static constexpr const char* lvlsHistoryRangeLowParameterId = "lvls/history/range-low";
    static constexpr const char* lvlsHistoryRangeHighParameterId = "lvls/history/range-high";
    static constexpr const char* scopTimeParameterId = "scop/time";
    static constexpr const char* scopTimeBaseParameterId = "scop/time-base";
    static constexpr const char* scopNoteLengthParameterId = "scop/note-length";
    static constexpr const char* scopStyleParameterId = "scop/style";
    static constexpr const char* scopOpacityParameterId = "scop/opacity";
    static constexpr const char* scopHorizontalZoomControlsParameterId = "scop/zoom-horiz";
    static constexpr const char* scopVerticalZoomControlsParameterId = "scop/zoom-vert";
    static constexpr const char* scopVerticalReadoutsParameterId = "scop/ro-vert";
    static constexpr const char* scopHorizontalReadoutsParameterId = "scop/ro-horiz";
    static constexpr const char* scopMonitorControlsParameterId = "scop/monitor";
    static constexpr const char* scopToolsParameterId = "scop/tools";
    static constexpr const char* specFftSizeParameterId = "spec/freq/fft-size";
    static constexpr const char* specFftOverlapParameterId = "spec/freq/fft-overlap";
    static constexpr const char* specMapTimeOverlapParameterId = "spec/map/time-overlap";
    static constexpr const char* specMapTimeParameterId = "spec/map/time";
    static constexpr const char* specMapTimeBaseParameterId = "spec/map/time-base";
    static constexpr const char* specMapNoteLengthParameterId = "spec/map/note-length";
    static constexpr const char* specMapColourMapParameterId = "spec/map/palette";
    static constexpr const char* specMapTimeRangeStartParameterId = "spec/map/time-range-start";
    static constexpr const char* specMapTimeRangeEndParameterId = "spec/map/time-range-end";
    static constexpr const char* specAverageTimeParameterId = "spec/freq/avg-time";
    static constexpr const char* specSmoothingParameterId = "spec/freq/smoothing";
    static constexpr const char* specFrequencyScaleParameterId = "spec/freq/freq-scale";
    static constexpr const char* specFilledDisplayParameterId = "spec/freq/filled";
    static constexpr const char* specSecondGraphParameterId = "spec/freq/graph-2";
    static constexpr const char* specFirstGraphTypeParameterId = "spec/freq/graph-1-type";
    static constexpr const char* specSecondGraphTypeParameterId = "spec/freq/graph-2-type";
    static constexpr const char* specFirstGraphColourParameterId = "spec/freq/graph-1-color";
    static constexpr const char* specSecondGraphColourParameterId = "spec/freq/graph-2-color";
    static constexpr const char* specGraphOpacityParameterId = "spec/freq/graph-opacity";
    static constexpr const char* specAntiAliasParameterId = "spec/freq/anti-alias";
    static constexpr const char* specHighQualityRenderingParameterId = "spec/map/hi-quality";
    static constexpr const char* specMapLeftToRightParameterId = "spec/map/left-to-right";
    static constexpr const char* specSlopeParameterId = "spec/freq/slope";
    static constexpr const char* specLowParameterId = "spec/freq/low";
    static constexpr const char* specHighParameterId = "spec/freq/high";
    static constexpr const char* specRangeLowParameterId = "spec/freq/range-low";
    static constexpr const char* specRangeHighParameterId = "spec/freq/range-high";
    static constexpr const char* specHorizontalReadoutsParameterId = "spec/freq/ro-horiz";
    static constexpr const char* specVerticalReadoutsParameterId = "spec/freq/ro-vert";
    static constexpr const char* specClearOnPlayParameterId = "spec/freq/clear-on-play";
    static constexpr const char* specCursorReadoutParameterId = "spec/freq/cur-horiz";
    static constexpr const char* specCursorVerticalReadoutParameterId = "spec/freq/cur-vert";
    static constexpr const char* specCursorNotesParameterId = "spec/freq/cur-notes";
    static constexpr const char* specMonitorControlsParameterId = "spec/freq/monitor";
    static constexpr const char* specHorizontalZoomParameterId = "spec/freq/zoom-horiz";
    static constexpr const char* specVerticalZoomParameterId = "spec/freq/zoom-vert";
    static constexpr const char* specMonitorModeParameterId = "spec/freq/monitor-mode";
    static constexpr const char* corrFftSizeParameterId = "corr/phase/fft-size";
    static constexpr const char* corrFftOverlapParameterId = "corr/phase/fft-overlap";
    static constexpr const char* corrAverageTimeParameterId = "corr/phase/avg-time";
    static constexpr const char* corrSmoothingParameterId = "corr/phase/smoothing";
    static constexpr const char* corrFrequencyScaleParameterId = "corr/phase/freq-scale";
    static constexpr const char* corrFilledDisplayParameterId = "corr/phase/filled";
    static constexpr const char* corrSecondGraphParameterId = "corr/phase/graph-2";
    static constexpr const char* corrFirstGraphTypeParameterId = "corr/phase/graph-1-type";
    static constexpr const char* corrSecondGraphTypeParameterId = "corr/phase/graph-2-type";
    static constexpr const char* corrFirstGraphColourParameterId = "corr/phase/graph-1-color";
    static constexpr const char* corrSecondGraphColourParameterId = "corr/phase/graph-2-color";
    static constexpr const char* corrGraphOpacityParameterId = "corr/phase/graph-opacity";
    static constexpr const char* corrClearOnPlayParameterId = "corr/phase/clear-on-play";
    static constexpr const char* corrHorizontalReadoutsParameterId = "corr/phase/ro-horiz";
    static constexpr const char* corrVerticalReadoutsParameterId = "corr/phase/ro-vert";
    static constexpr const char* corrCursorNotesParameterId = "corr/phase/cur-notes";
    static constexpr const char* corrCursorReadoutParameterId = "corr/phase/cur-horiz";
    static constexpr const char* corrCursorVerticalReadoutParameterId = "corr/phase/cur-vert";
    static constexpr const char* corrHorizontalZoomParameterId = "corr/phase/zoom-horiz";
    static constexpr const char* corrVerticalZoomParameterId = "corr/phase/zoom-vert";
    static constexpr const char* corrModeParameterId = "corr/mode";
    static constexpr const char* corrLowParameterId = "corr/phase/low";
    static constexpr const char* corrHighParameterId = "corr/phase/high";
    static constexpr const char* corrRangeLowParameterId = "corr/phase/range-low";
    static constexpr const char* corrRangeHighParameterId = "corr/phase/range-high";
    static constexpr const char* lvlsWidthParameterId = "lvls/peak-rms/width";
    static constexpr const char* lvlsPeakRangeHighParameterId = "lvls/peak-rms/range-high";
    static constexpr const char* lvlsPeakRangeLowParameterId = "lvls/peak-rms/range-low";
    static constexpr const char* lvlsRmsWindowMsParameterId = "lvls/peak-rms/rms-window";
    static constexpr const char* lvlsPeakHoldMsParameterId = "lvls/peak-rms/peak-hold";
    static constexpr const char* lvlsLoudnessRangeHighParameterId = "lvls/loudness/range-high";
    static constexpr const char* lvlsLoudnessRangeLowParameterId = "lvls/loudness/range-low";
    static constexpr const char* lvlsClearOnPlayParameterId = "lvls/peak-rms/clear-on-play";
    static constexpr const char* lvlsPeakRmsVisibleParameterId = "lvls/peak-rms/visible";
    static constexpr const char* lvlsLoudnessVisibleParameterId = "lvls/loudness/visible";
    static constexpr const char* lvlsHistoryVisibleParameterId = "lvls/history/visible";
    static constexpr const char* lvlsHistoryMomentaryVisibleParameterId = "lvls/history/momentary-visible";
    static constexpr const char* lvlsHistoryShortTermVisibleParameterId = "lvls/history/short-term-visible";
    static constexpr const char* lvlsHistoryIntegratedVisibleParameterId = "lvls/history/integrated-visible";
    static constexpr const char* lvlsHistoryHorizontalZoomParameterId = "lvls/history/zoom-horiz";
    static constexpr const char* lvlsHistoryVerticalZoomParameterId = "lvls/history/zoom-vert";
    static constexpr const char* lvlsHistoryHorizontalReadoutsParameterId = "lvls/history/ro-horiz";
    static constexpr const char* lvlsHistoryVerticalReadoutsParameterId = "lvls/history/ro-vert";
    static constexpr const char* lvlsPeakModeParameterId = "lvls/peak-rms/mode";
    static constexpr const char* lvlsMidSideModeParameterId = "lvls/peak-rms/ms-mode";
    static constexpr const char* lvlsHistorySoloParameterId = "lvls/history/solo";
    static constexpr const char* lvlsHistoryHorizontalStartParameterId = "lvls/history/horizontal-start";
    static constexpr const char* lvlsHistoryHorizontalEndParameterId = "lvls/history/horizontal-end";
    static constexpr const char* lvlsHistoryVerticalStartParameterId = "lvls/history/vertical-start";
    static constexpr const char* lvlsHistoryVerticalEndParameterId = "lvls/history/vertical-end";
    static constexpr const char* crossoverCountParameterId = "scop/cross-count";
    static constexpr const char* editorWidthStateKey = "ana.editor.width";
    static constexpr const char* editorHeightStateKey = "ana.editor.height";
    static constexpr const char* offlineSourceStateKey = "ana.offline.source";
    static constexpr const char* offlineTakeNumberStateKey = "ana.offline.takeNumber";
    static constexpr const char* offlineLocationStateKey = "ana.offline.location";
    static constexpr const char* offlineAutomaticRefreshStateKey = "ana.offline.autoRefresh";
    static constexpr const char* offlineKeepSecondTakeStateKey = "ana.offline.keepSecondTake";
    static constexpr const char* scopSingleViewStateKey = "ana.scop.singleView";
    static constexpr const char* scopFullSourceStateKey = "ana.scop.fullSource";
    inline static constexpr std::array<const char*, 3> lvlsSectionWeightStateKeys {
        "ana.lvls.section1", "ana.lvls.section2", "ana.lvls.section3"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopBandHeightStateKeys {
        "ana.scop.height1", "ana.scop.height2", "ana.scop.height3",
        "ana.scop.height4", "ana.scop.height5", "ana.scop.height6"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numCrossovers> crossoverParameterIds {
        "scop/cross-1", "scop/cross-2", "scop/cross-3", "scop/cross-4", "scop/cross-5"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopChannelModeParameterIds {
        "scop/band-1/mode", "scop/band-2/mode", "scop/band-3/mode",
        "scop/band-4/mode", "scop/band-5/mode", "scop/band-6/mode"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopVerticalZoomParameterIds {
        "scop/band-1/zoom", "scop/band-2/zoom", "scop/band-3/zoom",
        "scop/band-4/zoom", "scop/band-5/zoom", "scop/band-6/zoom"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopNormalizeParameterIds {
        "scop/band-1/normalize", "scop/band-2/normalize", "scop/band-3/normalize",
        "scop/band-4/normalize", "scop/band-5/normalize", "scop/band-6/normalize"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopRangeStartParameterIds {
        "scop/band-1/range-start", "scop/band-2/range-start", "scop/band-3/range-start",
        "scop/band-4/range-start", "scop/band-5/range-start", "scop/band-6/range-start"
    };
    inline static constexpr std::array<const char*, ana::dsp::LinkwitzRileyCrossover::numBands> scopRangeEndParameterIds {
        "scop/band-1/range-end", "scop/band-2/range-end", "scop/band-3/range-end",
        "scop/band-4/range-end", "scop/band-5/range-end", "scop/band-6/range-end"
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

    ana::spec::SpecProcessor& getSpecProcessor() noexcept { return specProcessor; }
    const ana::spec::SpecProcessor& getSpecProcessor() const noexcept { return specProcessor; }
    void clearSpecProcessor() noexcept { specProcessor.requestClear(); }
    ana::corr::CorrProcessor& getCorrProcessor() noexcept { return corrProcessor; }
    const ana::corr::CorrProcessor& getCorrProcessor() const noexcept { return corrProcessor; }
    void clearCorrProcessor() noexcept { corrProcessor.requestClear(); }
    ana::lvls::LvlsProcessor& getLvlsProcessor() noexcept { return lvlsProcessor; }
    const ana::lvls::LvlsProcessor& getLvlsProcessor() const noexcept { return lvlsProcessor; }
    void clearLvlsProcessor(int section = -1) noexcept
    {
        if (section < 0 || section == 0) lvlsProcessor.requestClear();
        if (section < 0 || section == 1) lvlsLoudnessProcessor.requestClear();
        if (section < 0 || section == 2) lvlsHistoryProcessor.requestClear();
    }
    void setLvlsFrozen(bool frozen) noexcept
    {
        lvlsProcessor.setFrozen(frozen);
        lvlsLoudnessProcessor.setFrozen(frozen);
        lvlsHistoryProcessor.setFrozen(frozen);
    }
    ana::MultibandScop& getMultibandScop() noexcept { return multibandScop; }
    ana::lvls::LvlsProcessor& getLvlsLoudnessProcessor() noexcept { return lvlsLoudnessProcessor; }
    ana::lvls::LvlsProcessor& getLvlsHistoryProcessor() noexcept { return lvlsHistoryProcessor; }
    OscSettings getOscSettings() const;
    bool setOscSettings(const OscSettings&);
    bool isOscInputPortBusy() const;
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters; }
    bool isOfflineSourceAvailable() const noexcept;
    int getOfflineAnalysisProgress() const noexcept;
    void cancelOfflineAnalysis();
    void requestOfflineAnalysis(size_t columnCount, bool forceRefresh = false,
                                bool specMapMode = false, size_t specMapRowCount = 2);
    std::shared_ptr<const ana::offline::AnalysisResult> getAnalysisResult() const;
    juce::Image getCachedOfflineSpectrogramImage() const;
    void setCachedOfflineSpectrogramImage(const juce::Image& image);
    std::vector<ana::offline::SourceChoice> getSourceChoices() const;
    juce::String getSelectedOfflineSourceId() const;
    int getSelectedOfflineTakeNumber() const noexcept;
    bool isOfflineLocationAvailable() const;
    bool isOfflineMonitorLocation() const noexcept;
    int getOfflineTrackNumber() const;
    juce::String getOfflineTrackName() const;
    void setOfflineMonitorLocation(bool monitor);
    void refreshOfflineSelection(const std::vector<ana::offline::SourceChoice>& choices);
    bool isOfflineAutomaticRefresh() const noexcept;
    void setOfflineAutomaticRefresh(bool automatic);
    void forceOfflineRefresh();
    bool isOfflineKeepSecondTake() const noexcept;
    void setOfflineKeepSecondTake(bool keep);
    double getScopTimeMilliseconds() const noexcept;
    bool isScopTimeNoteBased() const noexcept;
    double getSpecMapTimeMilliseconds() const noexcept;
    bool isSpecMapTimeNoteBased() const noexcept;
    bool isScopFilledStyle() const noexcept;
    float getScopOpacity() const noexcept;
    bool areScopHorizontalZoomControlsVisible() const noexcept;
    bool areScopVerticalZoomControlsVisible() const noexcept;
    bool areScopVerticalReadoutsVisible() const noexcept;
    bool areScopHorizontalReadoutsVisible() const noexcept;
    bool areScopMonitorControlsVisible() const noexcept;
    bool areScopToolsVisible() const noexcept;
    size_t getCrossoverCount() const noexcept;
    ana::ScopChannelMode getScopChannelMode(size_t bandIndex) const noexcept;
    float getScopVerticalZoomDecibels(size_t bandIndex) const noexcept;
    bool isScopBandNormalized(size_t bandIndex) const noexcept;
    ana::dsp::LinkwitzRileyCrossover::CrossoverFrequencies getCrossoverFrequencies() const noexcept;
    juce::Point<int> getLastEditorSize() const noexcept;
    std::array<float, 3> getLvlsSectionWeights() const noexcept;
    std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands> getScopBandHeightWeights() const noexcept;
    int getScopSingleViewBand() const noexcept;
    bool isScopFullSourceView() const noexcept;
    ana::AnalyzerPage getAnalyzerPageState() const noexcept;
    bool isSpecMapView() const noexcept;
    bool isCleanView() const noexcept;
    const char* resolveParameterId(const char* id) const noexcept;
    std::atomic<float>* getRawParameterValue(const char* id) const noexcept;
    juce::RangedAudioParameter* getActiveParameter(const char* id) const noexcept;
    void setSpecMonitorMode(int mode);
    void setCorrMode(int mode);
    void setLastEditorSize(int width, int height) noexcept;
    void setLvlsSectionWeights(const std::array<float, 3>& weights);
    void setScopBandHeightWeights(const std::array<float, ana::dsp::LinkwitzRileyCrossover::numBands>& weights);
    void setAnalyzerPageState(ana::AnalyzerPage page);
    void setSpecMapView(bool shouldUseMap);
    void setCrossoverCount(size_t crossoverCount);
    void setSelectedOfflineSourceId(const juce::String& sourceId);
    void setSelectedOfflineTakeNumber(int takeNumber);
    void setScopSingleViewBand(int bandIndex);
    void setScopFullSourceView(bool shouldShowFullSource);
    void setScopChannelMode(size_t bandIndex, ana::ScopChannelMode mode);
    void setScopVerticalZoomDecibels(size_t bandIndex, float decibels);
    void setScopBandNormalized(size_t bandIndex, bool shouldNormalize);

private:
    bool isParameterEnabled(const char* parameterId) const noexcept;
    void syncOfflineAnalysisSources(bool force = false);
    void constrainCorrRangeForMode(int mode);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::VST3ClientExtensions* getVST3ClientExtensions() override;
    void setIHostApplication(Steinberg::FUnknown* hostApplication) override;

    juce::AudioProcessorValueTreeState parameters;
    ana::spec::SpecProcessor specProcessor;
    ana::corr::CorrProcessor corrProcessor;
    ana::lvls::LvlsProcessor lvlsProcessor;
    ana::MultibandScop multibandScop;
    ana::lvls::LvlsProcessor lvlsLoudnessProcessor;
    ana::lvls::LvlsProcessor lvlsHistoryProcessor;
    std::unique_ptr<OscController> oscController;
    bool hostWasPlaying = false;
    bool processingOffline = false;
    std::atomic<double> hostTempoBpm { 120.0 };
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
    mutable juce::CriticalSection offlineSelectionLock;
    mutable juce::CriticalSection offlineSpectrogramImageLock;
    juce::Image cachedOfflineSpectrogramImage;
    juce::String selectedOfflineSourceId;
    int selectedOfflineTakeNumber = 1;
    std::atomic<bool> offlineMonitorLocation { false };
    std::atomic<bool> offlineAutomaticRefresh { true };
    std::atomic<bool> offlineKeepSecondTake { false };
    std::atomic<bool> offlineRefreshPending { false };
    std::vector<ana::offline::SourceChoice> offlineAnalysisSourceChoices;
    int offlineAnalysisTrackNumber = 0;
    juce::String offlineAnalysisTrackName;
    bool offlineAnalysisSourcesInitialised = false;
    ReaperHostBridge reaperHostBridge;
    // Analysis follows selection independently of the instance's audio playback.
    ReaperHostBridge monitorHostBridge;
    // Monitoring FX may have REAPER access but no ARA document/renderer roles.
    ana::offline::Worker hostAnalysisWorker;
};
