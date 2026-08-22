#pragma once

#include "MultibandScope.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>

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
    void drawLinearSlider(juce::Graphics& graphics,
                          int x, int y, int width, int height,
                          float sliderPosition, float minimumSliderPosition, float maximumSliderPosition,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
};

class AnaParameterControl final : public juce::Component
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
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    juce::String titleText;
    Formatter valueFormatter;
    AnaSliderLookAndFeel sliderLookAndFeel;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> valueBounds;
    bool interactionEnabled = true;
    bool compact = false;
};

class AnaMultibandScopeComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaMultibandScopeComponent(AnaAudioProcessor& processorRef);

    void setFrozen(bool shouldFreeze);
    void clearHistory();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void resetHistory();
    void clearBandHistory(size_t bandIndex);
    void resetColumnAccumulator();
    void appendHistoryColumn(size_t activeBandCount);
    void refreshBandModeButtons();
    float getDisplayedSample(size_t bandIndex, size_t sampleIndex) const noexcept;

    AnaAudioProcessor& processor;
    ana::MultibandScope::Snapshot incomingSamples;
    std::array<std::array<std::unique_ptr<AnaScopeButton>, 4>, ana::MultibandScope::numBands> bandModeButtons;
    std::array<std::unique_ptr<AnaScopeButton>, ana::MultibandScope::numBands> bandClearButtons;
    std::array<ana::ScopeChannelMode, ana::MultibandScope::numBands> historyChannelModes {};
    juce::Image historyImage;
    std::array<float, ana::MultibandScope::numBands> columnMinimums;
    std::array<float, ana::MultibandScope::numBands> columnMaximums;
    uint64_t readCursor = 0;
    double columnSampleProgress = 0.0;
    double historyTimeMilliseconds = 0.0;
    size_t historyBandCount = 0;
    bool frozen = false;
};

class AnaCrossoverSettingsComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AnaCrossoverSettingsComponent(AnaAudioProcessor& processorRef);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeActiveSplitCount(int delta);
    void constrainFrequency(size_t crossoverIndex);
    void refreshExternalState();

    AnaAudioProcessor& processor;
    AnaScopeButton addCrossoverButton { "XOV-ADD" };
    AnaScopeButton removeCrossoverButton { "XOV-DEL" };
    std::array<std::unique_ptr<AnaParameterControl>, ana::dsp::Crossover::numSplits> crossoverControls;
    bool constrainingFrequency = false;
};

class AnaAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnaAudioProcessorEditor(AnaAudioProcessor& processorRef);
    ~AnaAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showCrossoverSettings(bool shouldShowSettings);

    AnaAudioProcessor& audioProcessor;
    AnaMultibandScopeComponent scopeDisplay;
    AnaCrossoverSettingsComponent crossoverSettings;
    AnaParameterControl timeControl;

    AnaScopeButton frequencyButton { "FREQUENCY" };
    AnaScopeButton phaseButton { "PHASE" };
    AnaScopeButton scopeButton { "SCOPE" };
    AnaScopeButton settingsButton { "SETTINGS" };
    AnaScopeButton clearButton { "CLEAR" };
    AnaScopeButton freezeButton { "FREEZE" };
    bool showingCrossoverSettings = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnaAudioProcessorEditor)
};
