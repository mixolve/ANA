#include "ParameterControl.h"
#include "Theme.h"
#include "Processor.h"

#include <algorithm>
#include <utility>

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterId,
                                   juce::String title,
                                   Formatter formatter)
    : parameterState(state), sourceParameterId(parameterId),
      titleText(std::move(title)), valueFormatter(std::move(formatter)), valueBox(slider), compact(titleText.isEmpty())
{
    pressGesture.onArmed = [this]
    {
        if (onResetRequested)
            onResetRequested(*this);

        // Cancel the armed gesture so mouse-up cannot perform a second action.
        pressGesture.cancel();
        repaint();
    };
    slider.onValueChange = [this]
    {
        repaint();

        if (! updatingBinding && onValueChanged)
            onValueChanged();
    };
    valueBox.displayTextProvider = [this]
    { return interactionEnabled || displayValueWhenDisabled ? slider.getTextFromValue(slider.getValue()) : juce::String("OFF"); };
    valueBox.onInteractionBegin = [this] { refreshBinding(); };
    valueBox.onClick = [this] { activateValue(); };
    valueBox.onResetRequested = [this] { resetToDefault(); };
    valueBox.onChoiceClicked = [this] (int direction)
    {
        refreshBinding();
        const auto count=choiceParameter!=nullptr ? choiceParameter->choices.size() : boolParameter!=nullptr ? 2 : discreteSteps;
        if(!interactionEnabled || count<2) return;
        const auto next=(juce::roundToInt(slider.getValue()-slider.getMinimum())+direction+count)%count;
        juce::Slider::ScopedDragNotification gesture(slider);
        slider.setValue(slider.getMinimum()+next,juce::sendNotificationSync);
        if (onUserChoiceChanged) onUserChoiceChanged();
    };
    valueBox.onDiscreteEdited = [this]
    { if (onUserChoiceChanged) onUserChoiceChanged(); };
    addAndMakeVisible(valueBox);
    valueBox.onEditorVisibilityChanged=[this](bool editing)
    { if(onTextEditingChanged) onTextEditingChanged(editing); };
    valueBox.bindHostSlots(&static_cast<PluginProcessor&>(parameterState.processor).getHostSlots(),[this] { refreshBinding(); return getParameterId(); });
    refreshBinding();
    // Attachments propagate parameter values; this timer only resolves dynamic
    // FREQ/MAP/CORR submodule parameter IDs. No 30 Hz polling is necessary.
    startTimerHz(10);
}

ParameterControl::~ParameterControl()
{
    stopTimer();
    pressGesture.cancel();
}

void ParameterControl::refreshBinding()
{
    if (updatingBinding)
        return;
    auto& processor = static_cast<PluginProcessor&>(parameterState.processor);
    const auto id = fixedBinding ? sourceParameterId
        : juce::String(processor.resolveParameterId(sourceParameterId.toRawUTF8()));
    if (boundParameterId == id)
        return;
    const juce::ScopedValueSetter<bool> blockNotifications(updatingBinding, true);
    valueBox.cancelGesture();
    valueBox.hideEditor(true);
    pressGesture.cancel();
    valueBox.setNumericControlEnabled(false);
    attachment.reset();
    boundParameterId = id;
    parameter = parameterState.getParameter(id);
    choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter);
    boolParameter = dynamic_cast<juce::AudioParameterBool*>(parameter);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        parameterState, id, slider);
    if (valueParser)
        slider.valueFromTextFunction = valueParser;
    slider.textFromValueFunction = [this] (const double value)
    {
        return valueFormatter != nullptr ? valueFormatter(value) : juce::String(value);
    };
    valueBox.setNumericControlEnabled(rangeAdjustmentEnabled && supportsRangeAdjustment());
    valueBox.setDiscreteChoiceCount(choiceParameter!=nullptr ? choiceParameter->choices.size() : boolParameter!=nullptr ? 2 : discreteSteps);
    repaint();
}

void ParameterControl::setValueParser(Parser parser)
{
    valueParser = std::move(parser);
    // Rebuild the binding so clearing an override restores the active parameter's parser.
    boundParameterId.clear();
    refreshBinding();
}

void ParameterControl::setInteractionEnabled(const bool shouldEnable,
                                             const bool showValueWhenDisabled)
{
    if (interactionEnabled == shouldEnable
        && displayValueWhenDisabled == showValueWhenDisabled)
        return;

    interactionEnabled = shouldEnable;
    if (! interactionEnabled)
    {
        titleHovered = false;
        titleHover.setHovered(false);
    }
    valueBox.setInteractionEnabled(shouldEnable);
    displayValueWhenDisabled = showValueWhenDisabled;

    repaint();
}

juce::StringArray ParameterControl::getChoiceNames() const
{
    return choiceParameter != nullptr ? choiceParameter->choices : juce::StringArray();
}

int ParameterControl::getSelectedChoiceIndex() const noexcept
{
    return choiceParameter != nullptr ? juce::roundToInt(slider.getValue()) : -1;
}

void ParameterControl::setSelectedChoiceIndex(const int choiceIndex)
{
    refreshBinding();
    if (choiceParameter == nullptr || ! juce::isPositiveAndBelow(choiceIndex, choiceParameter->choices.size()))
        return;

    slider.setValue(choiceIndex, juce::sendNotificationSync);
    if (onUserChoiceChanged) onUserChoiceChanged();
}

void ParameterControl::setRangeAdjustmentEnabled(bool enabled)
{
    rangeAdjustmentEnabled = enabled;
    valueBox.setNumericControlEnabled(enabled && supportsRangeAdjustment());
}

void ParameterControl::setCompact(const bool shouldUseCompactLayout)
{
    if (compact == shouldUseCompactLayout)
        return;

    compact = shouldUseCompactLayout;
    resized();
    repaint();
}

void ParameterControl::setSquareStyle(bool enabled)
{
    squareStyle=enabled;
    valueBox.setSquareStyle(enabled ? std::function<juce::String()>([this] { return titleText; }) : nullptr);
    resized(); repaint();
}

void ParameterControl::paint(juce::Graphics& graphics)
{
    if(squareStyle) return;
    const auto hoverAmount = titleHover.amount(interactionEnabled && titleHovered);
    ControlAppearance style(false, hoverAmount);
    style.paint(graphics,titleBounds);
    graphics.setFont(ana::ui::makeFont());
    graphics.setColour(interactionEnabled?style.contentColour():ana::ui::light);
    const auto nameScrolling = ana::ui::drawLoopingText(graphics, uppercaseText(titleText),
        titleBounds.reduced(ana::ui::textPadding, 1), ana::ui::makeFont(), juce::Justification::centredLeft);
    if (nameScrolling) ana::ui::scheduleMarqueeRepaint(*this, marqueePending);
}

void ParameterControl::resized()
{
    auto row = getLocalBounds();

    if(squareStyle)
    {
        titleBounds={};
        valueBounds=row;
    }
    else if (! compact)
    {
        const auto availableWidth = std::max(0, row.getWidth() - ana::ui::gap.pixels());
        titleBounds = row.removeFromLeft(availableWidth / 2);
        titleBounds.setWidth(std::max(0, titleBounds.getWidth() - titleTrailingInset));
        ana::ui::gap.removeFromLeft(row);
        valueBounds = row;
    }
    else
    {
        titleBounds = {};
        valueBounds = row;
    }
    valueBox.setBounds(valueBounds);
}

void ParameterControl::mouseDown(const juce::MouseEvent& event)
{
    refreshBinding();
    if (! interactionEnabled || ! event.mods.isLeftButtonDown())
        return;

    if (titleBounds.contains(event.getPosition())) pressGesture.begin(parameter!=nullptr);

    repaint();
}

void ParameterControl::mouseEnter(const juce::MouseEvent& event)
{
    mouseMove(event);
}

void ParameterControl::mouseMove(const juce::MouseEvent& event)
{
    const auto hovered = interactionEnabled && titleBounds.contains(event.getPosition());
    if (titleHovered != hovered)
    {
        titleHovered = hovered;
        titleHover.setHovered(hovered);
        repaint();
    }
}

void ParameterControl::mouseDrag(const juce::MouseEvent& event)
{
    if (! pressGesture.isActive())
        return;

    if (event.mouseWasDraggedSinceMouseDown())
    {
        pressGesture.markDragged();
        repaint();
    }
}

void ParameterControl::mouseUp(const juce::MouseEvent&)
{
    if (! pressGesture.isActive())
        return;

    pressGesture.release();
    repaint();
}

void ParameterControl::activateValue()
{
    refreshBinding();
    if (!interactionEnabled) return;
    if (choiceParameter != nullptr)
    {
        if (onChoiceRequested)
            onChoiceRequested(*this);
    }
    else if (boolParameter != nullptr)
    {
        slider.setValue(slider.getValue() < 0.5 ? 1.0 : 0.0, juce::sendNotificationSync);
    }
    else
    {
        valueBox.showEditor();
    }
}

void ParameterControl::cancelPendingEditor()
{
    valueBox.cancelGesture();
    valueBox.hideEditor(true);
}

void ParameterControl::mouseExit(const juce::MouseEvent&)
{
    titleHovered = false;
    titleHover.setHovered(false);

    if (! pressGesture.isActive())
    {
        repaint();
        return;
    }

    pressGesture.cancelArming();
    repaint();
}

void ParameterControl::resetToDefault()
{
    refreshBinding();
    if (parameter == nullptr || ! interactionEnabled)
        return;

    juce::Slider::ScopedDragNotification gesture(slider);
    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                    juce::sendNotificationSync);
}
