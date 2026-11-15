#pragma once

#include <JuceHeader.h>
#include "Controls.h"
#include "NumericValueBox.h"

#include <functional>
#include <memory>

class ParameterControl final : public juce::Component, private juce::Timer
{
public:
    using Formatter = std::function<juce::String(double)>;
    using Parser = std::function<double(const juce::String&)>;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterId,
                     juce::String title,
                     Formatter formatter);
    ~ParameterControl() override;
    void syncDockCopy(ParameterControl& copy) { copy.setInteractionEnabled(interactionEnabled,displayValueWhenDisabled); }
    std::unique_ptr<ParameterControl> createDockCopy()
    {
        auto copy=std::make_unique<ParameterControl>(parameterState,getParameterId(),titleText,valueFormatter);
        copy->setFixedBinding(true); copy->setValueParser(valueParser); if(discreteSteps>0) copy->setDiscreteSteps(discreteSteps);
        copy->setSquareStyle(true); copy->setDragUnitStep(dragUnitStep);
        copy->onValueChanged=onValueChanged;
        copy->onUserChoiceChanged=onUserChoiceChanged; copy->onTextEditingChanged=onTextEditingChanged;
        return copy;
    }

    juce::Slider& getSlider() noexcept { refreshBinding(); return slider; }
    void refreshBinding();
    void setFixedBinding(bool fixed)
    {
        if (fixedBinding == fixed) return;
        fixedBinding = fixed;
        boundParameterId.clear();
        refreshBinding();
        if (fixedBinding) stopTimer();
        else startTimerHz(10);
    }
    const juce::String& getSourceParameterId() const noexcept { return sourceParameterId; }
    juce::String getParameterId() const { return boundParameterId.isNotEmpty() ? boundParameterId : sourceParameterId; }
    void setValueParser(Parser parser);
    void setInteractionEnabled(bool shouldEnable, bool showValueWhenDisabled = false);
    bool supportsRangeAdjustment() const noexcept
    {
        return parameter != nullptr && choiceParameter == nullptr && boolParameter == nullptr;
    }
    juce::StringArray getChoiceNames() const;
    int getSelectedChoiceIndex() const noexcept;
    bool isDiscreteGestureActive() const noexcept { return valueBox.isDiscreteGestureActive(); }
    void setSelectedChoiceIndex(int choiceIndex);
    juce::Rectangle<int> getValueBounds() const noexcept { return valueBounds; }
    juce::Rectangle<int> getTitleBounds() const noexcept { return titleBounds; }
    void cancelPendingEditor();
    void setCompact(bool shouldUseCompactLayout);
    void setDiscreteSteps(int count) { discreteSteps=count; valueBox.setDiscreteChoiceCount(count); }
    void setSquareStyle(bool enabled);
    void setDragUnitStep(double step) { dragUnitStep = step; valueBox.setDragUnitStep(step); }
    void setSlotIndicatorVisible(bool visible) { valueBox.setSlotIndicatorVisible(visible); }
    void setMarking(std::function<bool()> selected,std::function<void()> toggle)
    { valueBox.isMarked=std::move(selected); valueBox.onMark=std::move(toggle); }
    bool isSquareStyle() const noexcept { return squareStyle; }
    void setRangeAdjustmentEnabled(bool enabled);
    void setTitleTrailingInset(int inset)
    {
        titleTrailingInset = juce::jmax(0, inset);
        resized();
        repaint();
    }
    void resetToDefault();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void(ParameterControl&)> onResetRequested;
    std::function<void()> onValueChanged;
    // Called only for a UI choice gesture, not for host state restoration.
    std::function<void()> onUserChoiceChanged;
    std::function<void(bool)> onTextEditingChanged;

private:
    void timerCallback() override
    {
        if (!fixedBinding && isShowing()) refreshBinding();
    }
    juce::AudioProcessorValueTreeState& parameterState;
    juce::String sourceParameterId;
    juce::String boundParameterId;

    void activateValue();
    juce::String titleText;
    Formatter valueFormatter;
    Parser valueParser;
    juce::Slider slider;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::AudioParameterChoice* choiceParameter = nullptr;
    juce::AudioParameterBool* boolParameter = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    NumericValueBox valueBox;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> valueBounds;
    bool rangeAdjustmentEnabled = true;
    bool interactionEnabled = true;
    bool updatingBinding = false;
    bool fixedBinding = false;
    bool displayValueWhenDisabled = false;
    bool marqueePending = false;
    int discreteSteps = 0;
    double dragUnitStep = 0.0;
    bool compact = false;
    bool squareStyle = false;
    int titleTrailingInset = 0;
    LongPressGesture pressGesture;
    bool titleHovered=false;
    HoverAnimation titleHover { *this, false };
};
