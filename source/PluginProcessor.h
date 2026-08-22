#pragma once

#include "MultibandScope.h"

#include <JuceHeader.h>
#include <array>

class AnaAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr const char* scopeTimeParameterId = "scopeTimeMs";
    static constexpr const char* activeSplitCountParameterId = "activeSplitCount";
    inline static constexpr std::array<const char*, ana::dsp::Crossover::numSplits> crossoverParameterIds {
        "xover1", "xover2", "xover3", "xover4", "xover5"
    };
    inline static constexpr std::array<const char*, ana::MultibandScope::numBands> scopeChannelModeParameterIds {
        "scopeBand1Mode", "scopeBand2Mode", "scopeBand3Mode",
        "scopeBand4Mode", "scopeBand5Mode", "scopeBand6Mode"
    };

    AnaAudioProcessor();
    ~AnaAudioProcessor() override = default;

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
    double getScopeTimeMilliseconds() const noexcept;
    size_t getActiveSplitCount() const noexcept;
    ana::ScopeChannelMode getScopeChannelMode(size_t bandIndex) const noexcept;
    ana::dsp::Crossover::SplitFrequencies getCrossoverFrequencies() const noexcept;
    void setActiveSplitCount(size_t splitCount);
    void setScopeChannelMode(size_t bandIndex, ana::ScopeChannelMode mode);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    ana::MultibandScope multibandScope;
};
