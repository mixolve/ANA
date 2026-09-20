#pragma once

#include <JuceHeader.h>
#include "Controls.h"

#include <functional>
#include <memory>

class ParameterControl final : public juce::Component
{
public:
    using Formatter = std::function<juce::String(double)>;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterId,
                     juce::String title,
                     Formatter formatter);
    ~ParameterControl() override;

    juce::Slider& getSlider() noexcept { return slider; }
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
    void resetToDefault();
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    std::function<void(ParameterControl&)> onFocusRequested;
    std::function<void(ParameterControl&)> onChoiceRequested;
    std::function<void(ParameterControl&)> onResetRequested;
    std::function<void()> onValueChanged;
    std::function<void(bool)> onTextEditingChanged;

private:
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
    bool displayValueWhenDisabled = false;
    bool selected = false;
    bool compact = false;
    bool pressHighlighted = false;
    bool valueEditorActive = false;
    LongPressGesture pressGesture;
    PressRegion pressRegion = PressRegion::none;
    PressRegion hoverRegion = PressRegion::none;
};
