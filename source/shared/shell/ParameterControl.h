#pragma once

#include <JuceHeader.h>
#include "Controls.h"

#include <functional>
#include <memory>

class ParameterControl final : public juce::Component, private juce::Timer
{
public:
    using Formatter = std::function<juce::String(double)>;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterId,
                     juce::String title,
                     Formatter formatter);
    ~ParameterControl() override;

    juce::Slider& getSlider() noexcept { refreshBinding(); return slider; }
    void refreshBinding();
    void setInteractionEnabled(bool shouldEnable, bool showValueWhenDisabled = false);
    bool isInteractionEnabled() const noexcept { return interactionEnabled; }
    bool supportsFocusedPotentiometer() const noexcept
    {
        return parameter != nullptr && choiceParameter == nullptr && boolParameter == nullptr;
    }
    juce::StringArray getChoiceNames() const;
    int getSelectedChoiceIndex() const noexcept;
    void setSelectedChoiceIndex(int choiceIndex);
    juce::Rectangle<int> getValueBounds() const noexcept { return valueBounds; }
    juce::Rectangle<int> getTitleBounds() const noexcept { return titleBounds; }
    void commitPendingEditor();
    void setSelected(bool shouldSelect);
    void setCompact(bool shouldUseCompactLayout);
    void setWheelSpeedMultiplier(float multiplier) noexcept { wheelSpeedMultiplier = multiplier; }
    void resetToDefault();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    std::function<void(ParameterControl&)> onFocusRequested;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void(ParameterControl&)> onResetRequested;
    std::function<void()> onValueChanged;
    std::function<void(bool)> onTextEditingChanged;

private:
    void timerCallback() override { refreshBinding(); }
    juce::AudioProcessorValueTreeState& parameterState;
    juce::String sourceParameterId;
    juce::String boundParameterId;
    enum class PressRegion { none, title, value };

    void showValueEditor();
    void hideValueEditor(bool discardChanges);
    juce::String titleText;
    Formatter valueFormatter;
    juce::Slider slider;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::AudioParameterChoice* choiceParameter = nullptr;
    juce::AudioParameterBool* boolParameter = nullptr;
    juce::TextEditor valueEditor;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> valueBounds;
    bool interactionEnabled = true;
    bool updatingBinding = false;
    bool displayValueWhenDisabled = false;
    bool selected = false;
    bool compact = false;
    bool pressHighlighted = false;
    bool valueEditorActive = false;
    bool wheelArmed = false;
    float wheelStepRemainder = 0.0f;
    float wheelSpeedMultiplier = 1.0f;
    LongPressGesture pressGesture;
    PressRegion pressRegion = PressRegion::none;
    PressRegion hoverRegion = PressRegion::none;
};
