#pragma once

#include "shared/shell/Controls.h"
#include "shared/shell/ParameterControl.h"

#include <cstdint>
#include <JuceHeader.h>

#include <functional>
#include <vector>

class PluginProcessor;

class CorrView final : public juce::Component, private juce::Timer
{
public:
    explicit CorrView(PluginProcessor& processorRef);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    juce::String getSettingsViewModeName() const;

private:
    void timerCallback() override;
    void syncRangeSliders();
    void updateFrequencyRangeFromSlider();
    void updateCorrRangeFromSlider();
    void refreshControls();
    void setCorrMode(int mode);
    int getCorrModeIndex() const noexcept;
    void drawCorr(juce::Graphics& graphics, const std::vector<float>& values,
                         const juce::Rectangle<float> plotBounds, float lowFrequency,
                         float highFrequency, float lowRange, float highRange,
                         double sampleRate, int fftSize, juce::Colour lineColour,
                         juce::Colour fillColour);
    juce::Rectangle<float> getPlotBounds() const noexcept;

    PluginProcessor& processor;
    ParameterControl frequencyLowControl;
    ParameterControl frequencyHighControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    EllipsisLabel cursorReadoutLabel;
    EllipsisLabel cursorVerticalReadoutLabel;
    ControlButton phaseModeButton { "PHASE" };
    ControlButton freqModeButton { "FREQ" };
    ControlButton signedModeButton { "SIGNED" };
    RangeSlider frequencyRangeSlider;
    RangeSlider corrRangeSlider { RangeSlider::Orientation::vertical };
    std::vector<float> primaryCorr;
    std::vector<float> secondaryCorr;
    uint64_t displayedRevision = 0;
    bool synchronisingRanges = false;
    juce::Point<float> cursorPosition;
    bool cursorInside = false;
    float lastCursorFrequency = 0.0f;
};
